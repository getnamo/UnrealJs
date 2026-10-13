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
// Typed arrays and ArrayBuffers anywhere in arguments and results travel as raw bytes, so frames,
// meshes or sample buffers move between threads without per-element JSON. (The game thread reads
// them through the 'memory' global when the context has it, i.e. the UnrealMemory feature.)
//
// Inside a worker, setTimeout / setInterval / setImmediate work, so a worker can free-run (e.g. a
// simulation loop) and still answer calls between batches. Pending timers keep a one-shot alive.
//
// Wire protocol with the worker: see async-worker.js.

(function () {
	'use strict';

	const host = Async;

	const RESERVED_CAPTURE_NAMES = new Set(['exports', '__worker', '__lambda']);
	const IDENTIFIER = /^[A-Za-z_$][A-Za-z0-9_$]*$/;

	const handlers = new Map();	// lambdaId -> LambdaHandle

	// --- payload codec (keep in sync with async-worker.js) ---

	const BINARY_TYPES = {
		ArrayBuffer, DataView, Int8Array, Uint8Array, Uint8ClampedArray, Int16Array, Uint16Array,
		Int32Array, Uint32Array, Float32Array, Float64Array, BigInt64Array, BigUint64Array
	};

	// -> { text, bytes }: JSON text, plus one packed Uint8Array if the value held binary data
	const encodePayload = (value) => {
		if (value === undefined) {
			return { text: '', bytes: undefined };
		}
		const blobs = [];
		let total = 0;
		const text = JSON.stringify(value, (key, v) => {
			if (v === null || typeof v !== 'object') {
				return v;
			}
			let view;
			if (ArrayBuffer.isView(v)) {
				view = new Uint8Array(v.buffer, v.byteOffset, v.byteLength);
			} else if (v instanceof ArrayBuffer) {
				view = new Uint8Array(v);
			} else {
				return v;
			}
			const offset = (total + 7) & ~7;	// keep every typed array aligned in the packed buffer
			total = offset + view.byteLength;
			blobs.push({ offset, view });
			return { $bytes: v.constructor.name, offset, byteLength: view.byteLength };
		});
		if (blobs.length === 0) {
			return { text: text === undefined ? '' : text, bytes: undefined };
		}
		const bytes = new Uint8Array(total);
		for (const blob of blobs) {
			bytes.set(blob.view, blob.offset);
		}
		return { text, bytes };
	};

	const decodePayload = (text, bytes) => {
		if (text === undefined || text === null || text === '') {
			return undefined;
		}
		try {
			if (!bytes) {
				return JSON.parse(text);
			}
			return JSON.parse(text, (key, v) => {
				if (v !== null && typeof v === 'object' && typeof v.$bytes === 'string' && BINARY_TYPES[v.$bytes]) {
					const Type = BINARY_TYPES[v.$bytes];
					const start = bytes.byteOffset + v.offset;
					if (Type === ArrayBuffer) {
						return bytes.buffer.slice(start, start + v.byteLength);
					}
					if (Type === DataView) {
						return new DataView(bytes.buffer, start, v.byteLength);
					}
					return new Type(bytes.buffer, start, v.byteLength / Type.BYTES_PER_ELEMENT);
				}
				return v;
			});
		} catch (e) {
			return text;
		}
	};

	// Bytes of the message being delivered, copied out of C++ (only valid during OnWorkerMessage)
	const takeMessageBytes = (numBytes) => {
		if (!(numBytes > 0)) {
			return undefined;
		}
		if (typeof memory !== 'undefined' && memory.access) {
			const external = memory.access(host.GetMessageBytes());
			const copy = new Uint8Array(numBytes);
			copy.set(new Uint8Array(external, 0, numBytes));
			memory.unbind(external);	// detach: the C++ array is released after this call
			return copy;
		}
		return Uint8Array.from(host.CopyMessageBytes());	// no 'memory' global: slower element-wise copy
	};

	const postToWorker = (lambdaId, kind, requestId, name, value) => {
		const { text, bytes } = encodePayload(value);
		return bytes
			? host.PostToWorkerWithBytes(lambdaId, kind, requestId, name, text, bytes)
			: host.PostToWorker(lambdaId, kind, requestId, name, text);
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

			const { text, bytes } = encodePayload(args);
			const requestId = ++this._nextRequestId;
			const promise = new Promise((resolve, reject) => this._pending.set(requestId, { resolve, reject, callback }));

			const posted = bytes
				? host.PostToWorkerWithBytes(this.lambdaId, 'call', requestId, functionName, text, bytes)
				: host.PostToWorker(this.lambdaId, 'call', requestId, functionName, text);
			if (!posted) {
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

		_onMessage(kind, requestId, name, payload, bytes) {
			Async.DevLog(`Async lambda ${this.lambdaId} <- ${kind} #${requestId} ${name}`);

			switch (kind) {
				case 'complete': {
					const info = decodePayload(payload, bytes) || {};
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
					const value = decodePayload(payload, bytes);
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
					const info = decodePayload(payload);
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
					this._onBridge(requestId, name, decodePayload(payload, bytes));
					break;
				case 'stopped':
					this._onStopped();
					break;
			}
		}

		_onBridge(requestId, name, args) {
			const fail = (e) => {
				this._reportError(e, false);
				if (!this.stopped) {
					host.PostToWorker(this.lambdaId, 'bridge-error', requestId, name, errorPayload(e));
				}
			};
			const send = (value) => {
				if (this.stopped) {
					return;
				}
				try {
					postToWorker(this.lambdaId, 'bridge-reply', requestId, name, value);
				} catch (e) {
					fail(e);
				}
			};

			const bridge = this.bridges[name];
			if (typeof bridge !== 'function') {
				fail(new Error(`no captured function '${name}'`));
				return;
			}

			let result;
			try {
				result = bridge(args);
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

	host.OnWorkerMessage = (kind, workerId, requestId, name, payload, numBytes) => {
		// Copy the bytes out first: they are released when this call returns
		const bytes = takeMessageBytes(numBytes);
		const handler = handlers.get(workerId);
		if (handler) {
			handler._onMessage(kind, requestId, name, payload, bytes);
		}
	};

	Async.instance = host;		// legacy alias
	Async.DevLog = () => {};	// assign console.log for message tracing
	Async.ParseArgs = (text) => decodePayload(text);

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
