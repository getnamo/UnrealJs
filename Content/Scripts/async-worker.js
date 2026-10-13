// Prelude for Async.Lambda workers (see async.js). Runs inside each worker's own isolate and
// thread, before the lambda's bootstrap source.
//
// Worker -> game thread: __worker.post(kind, requestId, name, payload, bytes?)
//   complete  (0, '', {result, exports, persistent})  lambda body finished
//   reply     (id, exportName, value)                 result of an export call
//   error     (id, name, {message, stack})            id 0 + '__main': the lambda body failed
//   bridge    (id, captureName, args)                 call a captured game-thread function
// Game thread -> worker: __worker.dispatch(kind, requestId, name, payload, bytes?), called from C++
//   call          (id, exportName, args)
//   bridge-reply  (id, '', value)
//   bridge-error  (id, '', {message, stack})
//   eval          (0, '', source)
// C++ also calls __worker.tick() each loop: it runs due timers and returns ms until the next one.
//
// Payloads are JSON text ('' means undefined) and are parsed, never evaluated. Typed arrays and
// ArrayBuffers anywhere in a value travel as raw bytes beside the JSON (codec below, which must
// match the copy in async.js).
(function (worker) {
	'use strict';

	// --- payload codec (keep in sync with async.js) ---

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

	// --- state ---

	globalThis.exports = {};

	const pendingBridges = new Map();
	let nextBridgeId = 0;
	let mainSettled = false;
	let keepAlive = false;

	const post = (kind, requestId, name, value) => {
		const { text, bytes } = encodePayload(value);
		worker.post(kind, requestId, name, text, bytes);
	};

	const errorInfo = (e) => ({
		message: (e && e.message !== undefined) ? String(e.message) : String(e),
		stack: (e && e.stack) ? String(e.stack) : undefined
	});

	const toError = (info) => {
		const error = new Error((info && info.message) || String(info));
		if (info && info.stack) {
			error.stack = info.stack;
		}
		return error;
	};

	const reportError = (requestId, name, e) => worker.post('error', requestId, name, JSON.stringify(errorInfo(e)));

	const exportCount = () => Object.keys(globalThis.exports || {}).length;

	// --- timers (setTimeout/setInterval/setImmediate), run by __worker.tick from the worker loop ---

	const timers = new Map();	// id -> { due, ms, fn, args, repeat }
	let nextTimerId = 0;

	const addTimer = (fn, ms, args, repeat) => {
		if (typeof fn !== 'function') {
			throw new TypeError('timer callback must be a function');
		}
		const delay = Math.max(0, Number(ms) || 0);
		const id = ++nextTimerId;
		timers.set(id, { due: Date.now() + delay, ms: delay, fn, args, repeat });
		return id;
	};
	const clearTimer = (id) => {
		timers.delete(id);
	};

	globalThis.setTimeout = (fn, ms, ...args) => addTimer(fn, ms, args, false);
	globalThis.setInterval = (fn, ms, ...args) => addTimer(fn, ms, args, true);
	globalThis.setImmediate = (fn, ...args) => addTimer(fn, 0, args, false);
	globalThis.clearTimeout = clearTimer;
	globalThis.clearInterval = clearTimer;
	globalThis.clearImmediate = clearTimer;

	// One-shot lambdas (no exports, not persisted) end once their body, bridge calls and timers have settled
	const maybeClose = () => {
		if (mainSettled && !keepAlive && pendingBridges.size === 0 && timers.size === 0 && exportCount() === 0) {
			worker.close();
		}
	};

	worker.tick = () => {
		if (timers.size === 0) {
			return -1;
		}

		// Only timers due now; ones added by these callbacks wait for the next tick
		const now = Date.now();
		const due = [];
		for (const [id, timer] of timers) {
			if (timer.due <= now) {
				due.push(id);
			}
		}
		due.sort((a, b) => timers.get(a).due - timers.get(b).due || a - b);

		for (const id of due) {
			const timer = timers.get(id);
			if (!timer) {
				continue;	// cleared by an earlier callback
			}
			if (timer.repeat) {
				timer.due = Date.now() + timer.ms;
			} else {
				timers.delete(id);
			}
			try {
				timer.fn(...timer.args);
			} catch (e) {
				reportError(0, '__timer', e);
			}
		}

		if (due.length > 0) {
			Promise.resolve().then(maybeClose);
		}
		if (timers.size === 0) {
			return -1;
		}
		let next = Infinity;
		for (const timer of timers.values()) {
			next = Math.min(next, timer.due);
		}
		return Math.max(0, next - Date.now());
	};

	// --- bridges: captured game-thread functions ---

	// Returns a promise, and also accepts a trailing callback
	worker.bridge = (name) => (args, callback) => {
		if (typeof args === 'function' && callback === undefined) {
			callback = args;
			args = undefined;
		}

		// Encode first so a bad argument throws here without leaving a pending entry behind
		const { text, bytes } = encodePayload(args);
		const requestId = ++nextBridgeId;
		const promise = new Promise((resolve, reject) => pendingBridges.set(requestId, { resolve, reject }));
		worker.post('bridge', requestId, name, text, bytes);

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

	const settleBridge = (requestId, text, bytes, isError) => {
		const pending = pendingBridges.get(requestId);
		if (!pending) {
			return;
		}
		pendingBridges.delete(requestId);

		if (isError) {
			pending.reject(toError(decodePayload(text)));
		} else {
			pending.resolve(decodePayload(text, bytes));
		}

		// Queued after the continuations above, so a callback that makes another bridge call keeps us alive
		Promise.resolve().then(maybeClose);
	};

	// --- export calls from the game thread ---

	const postReply = (requestId, name, value) => {
		try {
			post('reply', requestId, name, value);
		} catch (e) {
			reportError(requestId, name, e);
		}
	};

	const handleCall = (requestId, name, text, bytes) => {
		const exportsObject = globalThis.exports || {};
		const fn = exportsObject[name];
		if (typeof fn !== 'function') {
			reportError(requestId, name, new Error(`no exported function '${name}'`));
			return;
		}

		let result;
		try {
			result = fn.call(exportsObject, decodePayload(text, bytes));
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

	worker.dispatch = (kind, requestId, name, text, bytes) => {
		switch (kind) {
			case 'call':
				handleCall(requestId, name, text, bytes);
				break;
			case 'bridge-reply':
				settleBridge(requestId, text, bytes, false);
				break;
			case 'bridge-error':
				settleBridge(requestId, text, bytes, true);
				break;
			case 'eval':
				try {
					(0, eval)(text);
				} catch (e) {
					reportError(0, '__eval', e);
				}
				break;
			default:
				reportError(requestId, name, new Error(`unknown message kind '${kind}'`));
		}
	};

	// --- lambda body: called by the bootstrap source that async.js builds around the lambda ---

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
			try {
				post('complete', 0, '', { result: value, exports: exportNames, persistent: keepAlive || exportNames.length > 0 });
			} catch (e) {
				fail(e);
				return;
			}
			maybeClose();
		}, fail);
	};
})(__worker);
