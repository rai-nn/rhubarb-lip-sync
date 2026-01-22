#!/usr/bin/env node
/**
 * Test client for rhubarb-stream binary protocol
 *
 * Usage:
 *   node test-client.js                    # Run basic tests
 *   node test-client.js --audio <wav>      # Test with WAV file
 *   node test-client.js --help             # Show help
 */

const { spawn } = require('child_process');
const path = require('path');
const fs = require('fs');

// Frame types
const FrameType = {
	Audio: 0x01,
	Sentence: 0x02,
	Config: 0x03,
	Reset: 0x04,
	End: 0xff
};

/**
 * Create a binary frame
 * Format: [Type: 1 byte][Length: 4 bytes LE][Payload: N bytes]
 */
function createFrame(type, payload = Buffer.alloc(0)) {
	const header = Buffer.alloc(5);
	header.writeUInt8(type, 0);
	header.writeUInt32LE(payload.length, 1);
	return Buffer.concat([header, payload]);
}

/**
 * Create an audio frame from PCM samples
 */
function createAudioFrame(samples) {
	// samples is an array of 16-bit integers
	const payload = Buffer.alloc(samples.length * 2);
	for (let i = 0; i < samples.length; i++) {
		payload.writeInt16LE(samples[i], i * 2);
	}
	return createFrame(FrameType.Audio, payload);
}

/**
 * Create a sentence frame from JSON
 */
function createSentenceFrame(sentence) {
	const payload = Buffer.from(JSON.stringify(sentence), 'utf8');
	return createFrame(FrameType.Sentence, payload);
}

/**
 * Create a config frame from JSON
 */
function createConfigFrame(config) {
	const payload = Buffer.from(JSON.stringify(config), 'utf8');
	return createFrame(FrameType.Config, payload);
}

/**
 * Create an end frame
 */
function createEndFrame() {
	return createFrame(FrameType.End);
}

/**
 * Create a reset frame
 */
function createResetFrame() {
	return createFrame(FrameType.Reset);
}

/**
 * Run rhubarb-stream with the given frames
 */
async function runTest(frames, name) {
	return new Promise((resolve, reject) => {
		const binaryPath = path.join(__dirname, 'build', 'rhubarb-stream');

		if (!fs.existsSync(binaryPath)) {
			reject(new Error(`Binary not found: ${binaryPath}\nRun: cd build && cmake --build . --target rhubarb-stream`));
			return;
		}

		console.log(`\n=== Test: ${name} ===`);

		const proc = spawn(binaryPath, [], {
			stdio: ['pipe', 'pipe', 'pipe']
		});

		let stdout = '';
		let stderr = '';

		proc.stdout.on('data', (data) => {
			stdout += data.toString();
		});

		proc.stderr.on('data', (data) => {
			stderr += data.toString();
		});

		proc.on('close', (code) => {
			console.log('Output:');
			stdout.trim().split('\n').forEach(line => {
				try {
					const json = JSON.parse(line);
					console.log(`  ${json.type}: ${JSON.stringify(json)}`);
				} catch {
					console.log(`  (raw) ${line}`);
				}
			});

			if (stderr) {
				console.log('Stderr:', stderr);
			}

			console.log(`Exit code: ${code}`);
			resolve({ stdout, stderr, code });
		});

		proc.on('error', (err) => {
			reject(err);
		});

		// Send all frames
		const allFrames = Buffer.concat(frames);
		proc.stdin.write(allFrames);
		proc.stdin.end();
	});
}

/**
 * Run all tests
 */
async function runAllTests() {
	console.log('rhubarb-stream Protocol Test Client');
	console.log('====================================');

	// Test 1: Just END frame
	await runTest([
		createEndFrame()
	], 'END frame only');

	// Test 2: Audio + END
	await runTest([
		createAudioFrame([0, 1000, 2000, 3000, 2000, 1000, 0, -1000]),
		createEndFrame()
	], 'Audio chunk + END');

	// Test 3: Multiple audio chunks + END
	await runTest([
		createAudioFrame([0, 1000, 2000, 3000]),
		createAudioFrame([4000, 5000, 6000, 7000]),
		createEndFrame()
	], 'Multiple audio chunks + END');

	// Test 4: Audio + Sentence + END
	await runTest([
		createAudioFrame(new Array(16000).fill(0).map((_, i) => Math.sin(i * 0.1) * 10000)),
		createSentenceFrame({
			text: 'Hello world.',
			start: 0.0,
			end: 1.0,
			words: [
				{ text: 'Hello', start: 0.0, end: 0.4 },
				{ text: 'world.', start: 0.5, end: 1.0 }
			]
		}),
		createEndFrame()
	], 'Audio + Sentence + END');

	// Test 5: Config + Audio + Sentence + END
	await runTest([
		createConfigFrame({ sample_rate: 16000, debug: true }),
		createAudioFrame(new Array(16000).fill(0).map((_, i) => Math.sin(i * 0.1) * 10000)),
		createSentenceFrame({
			text: 'Testing configuration.',
			start: 0.0,
			end: 1.5,
			words: [
				{ text: 'Testing', start: 0.0, end: 0.6 },
				{ text: 'configuration.', start: 0.7, end: 1.5 }
			]
		}),
		createEndFrame()
	], 'Config + Audio + Sentence + END');

	// Test 6: Reset between streams
	await runTest([
		createAudioFrame([1000, 2000, 3000, 4000]),
		createResetFrame(),
		createAudioFrame([5000, 6000]),
		createEndFrame()
	], 'Audio + Reset + Audio + END');

	// Test 7: Multiple sentences
	await runTest([
		createAudioFrame(new Array(48000).fill(0).map((_, i) => Math.sin(i * 0.1) * 10000)),
		createSentenceFrame({
			text: 'First sentence.',
			start: 0.0,
			end: 1.0,
			words: [
				{ text: 'First', start: 0.0, end: 0.4 },
				{ text: 'sentence.', start: 0.5, end: 1.0 }
			]
		}),
		createSentenceFrame({
			text: 'Second sentence.',
			start: 1.5,
			end: 2.5,
			words: [
				{ text: 'Second', start: 1.5, end: 1.9 },
				{ text: 'sentence.', start: 2.0, end: 2.5 }
			]
		}),
		createEndFrame()
	], 'Audio + Multiple sentences + END');

	// Test 8: Zero-length audio frame (should be skipped)
	await runTest([
		createFrame(FrameType.Audio, Buffer.alloc(0)),  // Zero-length
		createAudioFrame([1000, 2000]),  // Normal audio
		createEndFrame()
	], 'Zero-length audio frame (should skip)');

	// Test 9: Invalid frame type
	await runTest([
		createFrame(0x99, Buffer.alloc(0)),  // Invalid type
		createEndFrame()
	], 'Invalid frame type (should error)');

	// Test 10: Malformed sentence JSON
	await runTest([
		createFrame(FrameType.Sentence, Buffer.from('not valid json', 'utf8')),
		createEndFrame()
	], 'Malformed sentence JSON (should continue)');

	console.log('\n====================================');
	console.log('All tests completed!');
}

// Run tests
runAllTests().catch(err => {
	console.error('Test failed:', err.message);
	process.exit(1);
});
