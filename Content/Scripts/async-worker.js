// Prelude for Async.Lambda workers (see async.js). Runs inside each worker's own isolate and
// thread, before the lambda's bootstrap source.
//
// Worker -> game thread: __worker.post(kind, requestId, name, payload)
//   complete  (0, '', {result, exports, persistent})  lambda body finished
//   reply     (id, exportName, value)                 result of an export call
//   error     (id, name, {message, stack})            id 0 + '__main': the lambda body failed
//   bridge    (id, captureName, args)                 call a captured game-thread function
// Game thread -> worker: __worker.dispatch(kind, requestId, name, payload), called from C++
//   call          (id, exportName, args)
//   bridge-reply  (id, '', value)
//   bridge-error  (id, '', {message, stack})
//   eval          (0, '', source)
// Payloads are JSON text ('' means undefined). They are parsed, never evaluated.
(function (worker) {
	'use strict';

	globalThis.exports = {};

	const pendingBridges = new Map();
	let nextBridgeId = 0;
	let mainSettled = false;
	let keepAlive = false;

	const parse = (text) => {
		if (text === undefined || text === null || text === '') {
			return undefined;
		}
		try {
			return JSON.parse(text);
		} catch (e) {
			return text;
		}
	};

	const stringify = (value) => {
		const text = JSON.stringify(value);
		return text === undefined ? '' : text;
	};

	const errorPayload = (e) => JSON.stringify({
		message: (e && e.message !== undefined) ? String(e.message) : String(e),
		stack: (e && e.stack) ? String(e.stack) : undefined
	});

	const toError = (payload) => {
		const info = parse(payload);
		const error = new Error((info && info.message) || String(info));
		if (info && info.stack) {
			error.stack = info.stack;
		}
		return error;
	};

	const reportError = (requestId, name, e) => worker.post('error', requestId, name, errorPayload(e));

	const exportCount = () => Object.keys(globalThis.exports || {}).length;

	// One-shot lambdas (no exports, not persisted) end once their body and bridge calls have settled
	const maybeClose = () => {
		if (mainSettled && !keepAlive && pendingBridges.size === 0 && exportCount() === 0) {
			worker.close();
		}
	};

	// A captured game-thread function: returns a promise, and also accepts a trailing callback
	worker.bridge = (name) => (args, callback) => {
		if (typeof args === 'function' && callback === undefined) {
			callback = args;
			args = undefined;
		}

		// Serialize first so a bad argument throws here without leaving a pending entry behind
		const payload = stringify(args);
		const requestId = ++nextBridgeId;
		const promise = new Promise((resolve, reject) => pendingBridges.set(requestId, { resolve, reject }));
		worker.post('bridge', requestId, name, payload);

		if (typeof callback === 'function') {
			// Bridge failures are already reported on the game thread; callbacks only see results
			promise.then((value) => {
				try {
					callback(value);
				} catch (e) {
					reportError(0, `${name} callback`, e);
				}
			}, () => {});
		}
		return promise;
	};

	const settleBridge = (requestId, payload, isError) => {
		const pending = pendingBridges.get(requestId);
		if (!pending) {
			return;
		}
		pendingBridges.delete(requestId);

		if (isError) {
			pending.reject(toError(payload));
		} else {
			pending.resolve(parse(payload));
		}

		// Queued after the continuations above, so a callback that makes another bridge call keeps us alive
		Promise.resolve().then(maybeClose);
	};

	const postReply = (requestId, name, value) => {
		let text;
		try {
			text = stringify(value);
		} catch (e) {
			reportError(requestId, name, e);
			return;
		}
		worker.post('reply', requestId, name, text);
	};

	const handleCall = (requestId, name, payload) => {
		const exportsObject = globalThis.exports || {};
		const fn = exportsObject[name];
		if (typeof fn !== 'function') {
			reportError(requestId, name, new Error(`no exported function '${name}'`));
			return;
		}

		let result;
		try {
			result = fn.call(exportsObject, parse(payload));
		} catch (e) {
			reportError(requestId, name, e);
			return;
		}

		if (result && typeof result.then === 'function') {
			result.then((value) => postReply(requestId, name, value), (e) => reportError(requestId, name, e));
		} else {
			postReply(requestId, name, result);
		}
	};

	worker.dispatch = (kind, requestId, name, payload) => {
		switch (kind) {
			case 'call':
				handleCall(requestId, name, payload);
				break;
			case 'bridge-reply':
				settleBridge(requestId, payload, false);
				break;
			case 'bridge-error':
				settleBridge(requestId, payload, true);
				break;
			case 'eval':
				try {
					(0, eval)(payload);
				} catch (e) {
					reportError(0, '__eval', e);
				}
				break;
			default:
				reportError(requestId, name, new Error(`unknown message kind '${kind}'`));
		}
	};

	// Called by the bootstrap source that async.js builds around the lambda
	worker.runMain = (lambda, persist) => {
		keepAlive = !!persist;

		const fail = (e) => {
			mainSettled = true;
			reportError(0, '__main', e);
			maybeClose();
		};

		let result;
		try {
			result = lambda();
		} catch (e) {
			fail(e);
			return;
		}

		Promise.resolve(result).then((value) => {
			mainSettled = true;
			const exportNames = Object.keys(globalThis.exports || {});
			let text;
			try {
				text = JSON.stringify({ result: value, exports: exportNames, persistent: keepAlive || exportNames.length > 0 });
			} catch (e) {
				fail(e);
				return;
			}
			worker.post('complete', 0, '', text);
			maybeClose();
		}, fail);
	};
})(__worker);
