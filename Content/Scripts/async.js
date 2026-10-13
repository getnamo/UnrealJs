// Async.Lambda: run a function on a background worker with its own thread and V8 isolate.
//
//   const lambda = Async.Lambda({value: 1, notify}, () => {
//       exports.work = (args) => { notify('working'); return args.a + value; };
//       return 'ready';
//   }, (result, lambda) => console.log(result));
//
//   lambda.exports.work({a: 2}, (r) => console.log(r));   // callback style
//   const r = await lambda.exports.work({a: 2});           // promise style, rejects with the worker's stack
//
// Captures are the lambda's only view of this scope: plain values are copied (JSON), functions
// become async bridges back to the game thread (callable as fn(args, callback) or await fn(args)),
// and UObjects are exposed in the worker as globals.
//
// Lifetime: a lambda that defines exports (or passes {persist: true}) stays alive to serve calls
// until lambda.stop(); otherwise its worker exits once the body and any bridge calls settle.
// Workers belong to this context's 'Async' object, which its owning component shuts down on
// reload and EndPlay, so a saved script never leaves old workers running.
//
// Wire protocol with the worker: see async-worker.js.

(function () {
	'use strict';

	const host = Async;

	const RESERVED_CAPTURE_NAMES = new Set(['exports', '__worker', '__lambda']);
	const IDENTIFIER = /^[A-Za-z_$][A-Za-z0-9_$]*$/;

	const handlers = new Map();	// lambdaId -> LambdaHandle

	const parsePayload = (text) => {
		if (text === undefined || text === null || text === '') {
			return undefined;
		}
		try {
			return JSON.parse(text);
		} catch (e) {
			return text;
		}
	};

	const stringifyPayload = (value) => {
		const text = JSON.stringify(value);
		return text === undefined ? '' : text;
	};

	const errorPayload = (e) => JSON.stringify({
		message: (e && e.message !== undefined) ? String(e.message) : String(e),
		stack: (e && e.stack) ? String(e.stack) : undefined
	});

	const errorFromInfo = (info) => {
		const error = new Error((info && info.message) || String(info));
		if (info && info.stack) {
			error.stack = info.stack;
		}
		return error;
	};

	const logError = (e) => console.error((e && e.stack) || String(e));

	const isUObject = (value) => value !== null && typeof value === 'object' && value.GetObjectClass !== undefined;

	class LambdaHandle {
		constructor(lambdaId, bridges, returnCallback) {
			this.lambdaId = lambdaId;
			this.bridges = bridges;
			this.pinned = false;	// true once the worker reports it stays alive (exports or persist)
			this.stopped = false;
			this.exportNames = [];

			// (error) => {}: receives worker errors. Unset, errors go to console.error
			this.onerror = undefined;

			this._returnCallback = returnCallback;
			this._pending = new Map();
			this._nextRequestId = 0;

			// Resolves with the lambda's return value; rejects if the body throws or the worker stops first
			this.ready = new Promise((resolve, reject) => {
				this._resolveReady = resolve;
				this._rejectReady = reject;
			});
			this.ready.catch(() => {});

			// Callable immediately: calls queue in the worker behind its bootstrap
			this.exports = new Proxy({}, {
				get: (target, name) => (typeof name === 'string' && name !== 'then')
					? (args, callback) => this.call(name, args, callback)
					: undefined,
				ownKeys: () => [...this.exportNames],
				getOwnPropertyDescriptor: (target, name) => this.exportNames.includes(name)
					? { configurable: true, enumerable: true, value: (args, callback) => this.call(name, args, callback) }
					: undefined
			});
		}

		// Calls exports[functionName](args) in the worker. Returns a promise; callback is optional
		call(functionName, args, callback) {
			if (typeof args === 'function' && callback === undefined) {
				callback = args;
				args = undefined;
			}

			if (this.stopped) {
				const error = new Error(`Async lambda ${this.lambdaId} is stopped; cannot call '${functionName}'`);
				this._reportError(error, false);
				return Promise.reject(error);
			}

			const payload = stringifyPayload(args);
			const requestId = ++this._nextRequestId;
			const promise = new Promise((resolve, reject) => this._pending.set(requestId, { resolve, reject, callback }));

			if (!host.PostToWorker(this.lambdaId, 'call', requestId, functionName, payload)) {
				// The worker already exited; its 'stopped' notification rejects this call
				Async.DevLog(`Async lambda ${this.lambdaId}: '${functionName}' posted after exit`);
			}
			return promise;
		}

		// Stops the worker (interrupting running script). Pending calls reject
		stop() {
			if (!this.stopped) {
				host.StopWorker(this.lambdaId);
			}
		}

		terminate() {
			this.stop();
		}

		// Exposes a UObject as a global in the worker; the worker keeps it alive until it exits
		exposeAs(object, name) {
			host.ExposeObjectInWorker(this.lambdaId, object, name);
		}

		// Runs raw source at the worker's global scope (fire and forget)
		runScript(source) {
			host.PostToWorker(this.lambdaId, 'eval', 0, '', source);
		}

		_reportError(error, alreadyLogged) {
			if (typeof this.onerror === 'function') {
				try {
					this.onerror(error);
				} catch (e) {
					logError(e);
				}
			} else if (!alreadyLogged) {
				console.error(`Async lambda ${this.lambdaId}: ${error.stack || error.message}`);
			}
		}

		_onMessage(kind, requestId, name, payload) {
			Async.DevLog(`Async lambda ${this.lambdaId} <- ${kind} #${requestId} ${name}`);

			switch (kind) {
				case 'complete': {
					const info = parsePayload(payload) || {};
					this.exportNames = info.exports || [];
					this.pinned = !!info.persistent;
					this._resolveReady(info.result);
					if (this._returnCallback) {
						try {
							this._returnCallback(info.result, this);
						} catch (e) {
							logError(e);
						}
					}
					break;
				}
				case 'reply': {
					const pending = this._pending.get(requestId);
					if (!pending) {
						break;
					}
					this._pending.delete(requestId);
					const value = parsePayload(payload);
					if (pending.callback) {
						try {
							pending.callback(value);
						} catch (e) {
							logError(e);
						}
					}
					pending.resolve(value);
					break;
				}
				case 'error': {
					const info = parsePayload(payload);
					const error = errorFromInfo(info);
					if (requestId > 0) {
						const pending = this._pending.get(requestId);
						if (pending) {
							this._pending.delete(requestId);
							pending.reject(error);
						}
					} else if (name === '__main') {
						this._rejectReady(error);
					}
					// Errors raised by the C++ side were already written to the log
					this._reportError(error, !!(info && info.logged));
					break;
				}
				case 'bridge':
					this._onBridge(requestId, name, payload);
					break;
				case 'stopped':
					this._onStopped();
					break;
			}
		}

		_onBridge(requestId, name, payload) {
			const reply = (kind, text) => {
				if (!this.stopped) {
					host.PostToWorker(this.lambdaId, kind, requestId, name, text);
				}
			};
			const fail = (e) => {
				this._reportError(e, false);
				reply('bridge-error', errorPayload(e));
			};
			const send = (value) => {
				let text;
				try {
					text = stringifyPayload(value);
				} catch (e) {
					fail(e);
					return;
				}
				reply('bridge-reply', text);
			};

			const bridge = this.bridges[name];
			if (typeof bridge !== 'function') {
				fail(new Error(`no captured function '${name}'`));
				return;
			}

			let result;
			try {
				result = bridge(parsePayload(payload));
			} catch (e) {
				fail(e);
				return;
			}

			if (result && typeof result.then === 'function') {
				result.then(send, fail);
			} else {
				send(result);
			}
		}

		_onStopped() {
			if (this.stopped) {
				return;
			}
			this.stopped = true;
			handlers.delete(this.lambdaId);

			const error = new Error(`Async lambda ${this.lambdaId} stopped`);
			for (const pending of this._pending.values()) {
				pending.reject(error);
			}
			this._pending.clear();
			this._rejectReady(error);	// no-op if the body already completed
		}
	}

	host.OnWorkerMessage = (kind, workerId, requestId, name, payload) => {
		const handler = handlers.get(workerId);
		if (handler) {
			handler._onMessage(kind, requestId, name, payload);
		}
	};

	Async.instance = host;		// legacy alias
	Async.DevLog = () => {};	// assign console.log for message tracing
	Async.ParseArgs = parsePayload;

	Async.TypeObj = (obj, showFullClass) => {
		if (showFullClass && typeof obj === 'object') {
			return Object.prototype.toString.call(obj);
		}
		if (obj == null) {
			return (obj + '').toLowerCase();
		}

		const deepType = Object.prototype.toString.call(obj).slice(8, -1).toLowerCase();
		if (deepType === 'generatorfunction') {
			return 'function';
		}

		return deepType.match(/^(array|bigint|date|error|function|generator|regexp|symbol)$/) ? deepType :
			(typeof obj === 'object' || typeof obj === 'function') ? 'object' : typeof obj;
	};

	// Async.Lambda(capture, fn, callback?, {enableRequire, persist}?) or Async.Lambda(fn, callback?, options?)
	// callback(result, lambda) runs on the game thread once the lambda body returns (or its promise resolves).
	Async.Lambda = (capture, rawFunction, callback, options) => {
		if (typeof capture === 'function') {
			options = callback;
			callback = rawFunction;
			rawFunction = capture;
			capture = {};
		}
		if (typeof rawFunction !== 'function') {
			throw new TypeError('Async.Lambda: expected a function to run');
		}
		const { enableRequire = false, persist = false } = options || {};

		const bridges = {};
		let declarations = '';
		host.ClearPreExposures();

		for (const key of Object.keys(capture || {})) {
			if (!IDENTIFIER.test(key) || RESERVED_CAPTURE_NAMES.has(key)) {
				throw new Error(`Async.Lambda: capture name '${key}' is not usable (must be an identifier, not ${[...RESERVED_CAPTURE_NAMES].join('/')})`);
			}

			const value = capture[key];
			if (typeof value === 'function') {
				bridges[key] = value;
				declarations += `const ${key} = __worker.bridge(${JSON.stringify(key)});\n`;
			} else if (isUObject(value)) {
				host.PreExposeObject(value, key);
			} else {
				declarations += `let ${key} = ${value === undefined ? 'undefined' : JSON.stringify(value)};\n`;
			}
		}

		// Captures are block-scoped around the lambda, so its source sees them as free variables
		const source = `(function () {\n${declarations}const __lambda = (${rawFunction.toString()});\n` +
			`__worker.runMain(__lambda, ${persist ? 'true' : 'false'});\n})();\n`;
		Async.DevLog(`Async.Lambda source:\n${source}`);

		const lambdaId = host.SpawnWorker(source, enableRequire);
		const handler = new LambdaHandle(lambdaId, bridges, callback);
		if (!lambdaId) {
			// Owner already shut down (stale script after a reload) or no thread available
			console.error('Async.Lambda: could not start a worker');
			handler._onStopped();
			return handler;
		}

		handlers.set(lambdaId, handler);
		return handler;
	};

	// Runs raw script text at a one-shot worker's global scope; callback gets the completion value
	Async.RunScript = (scriptText, callback) => Async.Lambda({ __script: String(scriptText) }, () => (0, eval)(__script), callback);

	Async.DevLog('async.js loaded');
})();
