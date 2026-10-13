// Async.Lambda example: Gray-Scott reaction-diffusion, one simulation per panel, each on its own
// worker thread (own V8 isolate).
//
// The workers free-run in a setTimeout loop and never wait for the game thread. The game thread
// only asks each worker for its latest frame (a Uint8Array that crosses threads as raw bytes)
// and uploads it to that panel's texture.
//
// Setup: run this as the DefaultScript ("examples/async/async_reaction_diffusion.js") of a
// JavascriptInstanceComponent with the features UnrealClasses, UnrealMemory, UnrealGlobals,
// UnrealMisc, Context, JsOwner and Async. The owning actor (JsOwner) provides:
//   Display0..3      StaticMeshComponents (a plane each) using the material
//                    /Engine/EngineMaterials/Widget3DPassThrough_Opaque
//   Label0..3        TextRenderComponents under the panels
//   TitleText, StatsText   TextRenderComponents
//   ReloadCount      an integer variable (shown in the stats; the Blueprint counts reloads)
// For live reload, add a JavascriptFileWatcherComponent and call the instance component's
// Reload() from its OnFilesChanged event. Then edit the settings below and save: the old workers
// stop and the script runs again with new ones.

// ---------------- settings: edit and save ----------------

// Cells per side for every simulation. The cost of a step grows with GRID * GRID
const GRID = 192;

// Steps a worker runs between checks for messages
const STEPS_PER_BATCH = 8;

// true: run the same simulations on the game thread instead (one batch per frame) and watch the
// game thread fps fall while the patterns crawl
const RUN_ON_GAME_THREAD = false;

// Gray-Scott presets. feed and kill pick the pattern (map: mrob.com/pub/comp/xmorphia)
const PANELS = [
	{ name: 'Mitosis', feed: 0.0367, kill: 0.0649, palette: 'ember' },
	{ name: 'Coral', feed: 0.0545, kill: 0.0620, palette: 'ocean' },
	{ name: 'Maze', feed: 0.0290, kill: 0.0570, palette: 'mint' },
	{ name: 'Worms', feed: 0.0780, kill: 0.0610, palette: 'violet' },
];

// Color stops [position 0..1, r, g, b] over the V concentration
const PALETTES = {
	ember: [[0, 10, 6, 18], [0.3, 70, 12, 40], [0.55, 210, 70, 20], [0.8, 255, 180, 50], [1, 255, 250, 215]],
	ocean: [[0, 5, 10, 30], [0.3, 10, 60, 115], [0.6, 30, 160, 190], [0.85, 160, 235, 230], [1, 250, 255, 255]],
	mint: [[0, 8, 18, 14], [0.4, 30, 120, 80], [0.7, 120, 220, 150], [1, 240, 255, 235]],
	violet: [[0, 12, 6, 24], [0.35, 80, 30, 140], [0.65, 200, 80, 200], [1, 255, 225, 255]],
};

// ---------------------------------------------------------

// Runs on a worker thread. Like a C++ lambda it only sees its captures:
// grid, steps, feed, kill, stops, seed
const simulate = () => {
	const n = grid;
	const cells = n * n;
	let u = new Float32Array(cells).fill(1);
	let v = new Float32Array(cells);
	let nextU = new Float32Array(cells);
	let nextV = new Float32Array(cells);

	// Seed squares of V (deterministic per panel)
	let state = (seed * 2654435761) >>> 0 || 1;
	const random = () => (state = (state * 1664525 + 1013904223) >>> 0) / 4294967296;
	for (let s = 0; s < 16; s++) {
		const cx = Math.floor(random() * n);
		const cy = Math.floor(random() * n);
		const r = 3 + Math.floor(random() * 6);
		for (let y = -r; y <= r; y++) {
			for (let x = -r; x <= r; x++) {
				const i = ((cy + y + n) % n) * n + ((cx + x + n) % n);
				u[i] = 0.5;
				v[i] = 0.25 + random() * 0.25;
			}
		}
	}

	// 256-entry BGRA lookup table from the palette stops
	const lut = new Uint8Array(256 * 4);
	for (let i = 0; i < 256; i++) {
		const t = i / 255;
		let k = 0;
		while (k < stops.length - 2 && t > stops[k + 1][0]) {
			k++;
		}
		const [t0, r0, g0, b0] = stops[k];
		const [t1, r1, g1, b1] = stops[k + 1];
		const f = Math.min(1, Math.max(0, (t - t0) / (t1 - t0)));
		lut[i * 4] = b0 + (b1 - b0) * f;
		lut[i * 4 + 1] = g0 + (g1 - g0) * f;
		lut[i * 4 + 2] = r0 + (r1 - r0) * f;
		lut[i * 4 + 3] = 255;
	}

	// One Gray-Scott step on a torus (Karl Sims' 3x3 Laplacian, Du = 1, Dv = 0.5, dt = 1)
	const step = () => {
		for (let y = 0; y < n; y++) {
			const row = y * n;
			const up = ((y + n - 1) % n) * n;
			const down = ((y + 1) % n) * n;
			for (let x = 0; x < n; x++) {
				const left = x === 0 ? n - 1 : x - 1;
				const right = x === n - 1 ? 0 : x + 1;
				const i = row + x;
				const uc = u[i];
				const vc = v[i];
				const lapU = 0.2 * (u[row + left] + u[row + right] + u[up + x] + u[down + x]) +
					0.05 * (u[up + left] + u[up + right] + u[down + left] + u[down + right]) - uc;
				const lapV = 0.2 * (v[row + left] + v[row + right] + v[up + x] + v[down + x]) +
					0.05 * (v[up + left] + v[up + right] + v[down + left] + v[down + right]) - vc;
				const uvv = uc * vc * vc;
				const nv = vc + 0.5 * lapV + uvv - (kill + feed) * vc;
				nextU[i] = uc + lapU - uvv + feed * (1 - uc);
				// Flush tiny V to 0: denormal float32s make the CPU slow down ~3x where V has died out
				nextV[i] = nv > 1e-20 ? nv : 0;
			}
		}
		[u, nextU] = [nextU, u];
		[v, nextV] = [nextV, v];
	};

	let generation = 0;
	let rateSteps = 0;
	let rateStart = Date.now();
	let stepsPerSecond = 0;

	// Free-running: a batch of steps, then yield so calls from the game thread can be answered
	const run = () => {
		for (let s = 0; s < steps; s++) {
			step();
		}
		generation += steps;
		rateSteps += steps;
		const now = Date.now();
		if (now - rateStart >= 500) {
			stepsPerSecond = Math.round(rateSteps * 1000 / (now - rateStart));
			rateSteps = 0;
			rateStart = now;
		}
		setTimeout(run, 0);
	};
	setTimeout(run, 0);

	// The game thread's view: the current state, colored. The buffer is reused; replies copy it
	const pixels = new Uint8Array(cells * 4);
	exports.frame = () => {
		for (let i = 0; i < cells; i++) {
			const t = Math.min(255, Math.max(0, (v[i] * 3.2 * 255) | 0)) * 4;
			const p = i * 4;
			pixels[p] = lut[t];
			pixels[p + 1] = lut[t + 1];
			pixels[p + 2] = lut[t + 2];
			pixels[p + 3] = 255;
		}
		return { pixels, generation, stepsPerSecond };
	};

	return `${n}x${n}`;
};

// Same lambda, run inline on the game thread: setTimeout batches run once per game frame
const runOnGameThread = (captures, fn) => {
	const names = Object.keys(captures);
	const workerExports = {};
	let batches = [];
	const factory = new Function(...names, 'exports', 'setTimeout', `return (${fn.toString()})();`);
	factory(...names.map((name) => captures[name]), workerExports, (callback) => batches.push(callback));
	return {
		stopped: false,
		tick: () => {
			const due = batches;
			batches = [];
			due.forEach((callback) => callback());
		},
		exports: { frame: () => Promise.resolve(workerExports.frame()) },
		stop: () => {},
	};
};

const owner = JsOwner;
const loadedAt = new Date().toTimeString().slice(0, 8);
const fixed = (value, digits) => Number(value).toFixed(digits);

const panels = PANELS.slice(0, 4).map((preset, index) => {
	const display = owner[`Display${index}`];
	const texture = JavascriptLibrary.CreateTransientTexture2D(GRID, GRID, true, false);
	const material = display.CreateDynamicMaterialInstance(0, null, 'None');
	material.SetTextureParameterValue('SlateUI', texture);

	const captures = {
		grid: GRID,
		steps: STEPS_PER_BATCH,
		feed: preset.feed,
		kill: preset.kill,
		stops: PALETTES[preset.palette] || PALETTES.ember,
		seed: index + 1,
	};

	const panel = { preset, texture, label: owner[`Label${index}`], inFlight: false, frame: undefined, shown: 0, error: undefined };
	panel.lambda = RUN_ON_GAME_THREAD ? runOnGameThread(captures, simulate) : Async.Lambda(captures, simulate);
	panel.lambda.onerror = (e) => {
		panel.error = e.message;
		console.error(`${preset.name}: ${e.stack || e.message}`);
	};
	return panel;
});

// Ask for the next frame as soon as the previous one has been shown: one request in flight per panel
const requestFrame = (panel) => {
	if (panel.inFlight || panel.lambda.stopped) {
		return;
	}
	panel.inFlight = true;
	panel.lambda.exports.frame().then((frame) => {
		panel.inFlight = false;
		JavascriptLibrary.UpdateTexture2D(panel.texture, frame.pixels);
		panel.frame = frame;
		panel.shown++;
	}, () => {
		panel.inFlight = false;
	});
};

const where = RUN_ON_GAME_THREAD ? 'on the GAME THREAD' : `on ${panels.length} worker threads`;
owner.TitleText.K2_SetText(`Async.Lambda: Gray-Scott reaction-diffusion ${where}`);

let frameSeconds = 1 / 60;
let sinceStats = 1;
let shownAtLastStats = panels.map(() => 0);

Context.OnTick = (deltaSeconds) => {
	frameSeconds += (deltaSeconds - frameSeconds) * 0.1;

	for (const panel of panels) {
		if (RUN_ON_GAME_THREAD) {
			panel.lambda.tick();
		}
		requestFrame(panel);
	}

	sinceStats += deltaSeconds;
	if (sinceStats < 0.25) {
		return;
	}

	panels.forEach((panel, index) => {
		const { name, feed, kill } = panel.preset;
		const frame = panel.frame || { generation: 0, stepsPerSecond: 0 };
		const shownPerSecond = (panel.shown - shownAtLastStats[index]) / sinceStats;
		shownAtLastStats[index] = panel.shown;
		const status = panel.error
			? `error: ${panel.error}`
			: `${frame.generation.toLocaleString()} steps, ${frame.stepsPerSecond.toLocaleString()}/s<br>${fixed(shownPerSecond, 0)} frames/s shown`;
		panel.label.K2_SetText(`${name}  f ${feed}  k ${kill}<br>${status}`);
	});

	owner.StatsText.K2_SetText(
		`game thread ${fixed(1 / frameSeconds, 0)} fps (${fixed(frameSeconds * 1000, 1)} ms)   ` +
		`worker threads live: ${JavascriptAsync.NumLiveWorkerThreads()}   ` +
		`grid ${GRID}x${GRID}   script loaded ${loadedAt}, reload #${owner.ReloadCount}`);
	sinceStats = 0;
};

console.log(`async_reaction_diffusion: ${panels.length} simulations ${where}, ${GRID}x${GRID}`);
