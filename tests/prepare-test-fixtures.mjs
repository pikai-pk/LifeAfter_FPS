// Reads the user's local supported archive; never writes to the game directory.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const home = process.env.FPS_TEST_HOME ? path.resolve(process.env.FPS_TEST_HOME) : path.join(root, '程序');
if (process.argv.length !== 4 || process.argv[2] !== '--archive') throw Error('Usage: node tests/prepare-test-fixtures.mjs --archive <local official script.py314.lc.npk>');
const { openArchive } = await import(pathToFileURL(path.join(home, 'engine/archive.mjs')));
const { decodeLz4 } = await import(pathToFileURL(path.join(home, 'engine/lz4-block.mjs')));
const { identifyModule } = await import(pathToFileURL(path.join(home, 'engine/patch.mjs')));
const recipe = JSON.parse(fs.readFileSync(path.join(home, 'engine/recipe.json'), 'utf8'));
const entry = openArchive(fs.realpathSync(process.argv[3]), recipe);
const decoded = decodeLz4(entry.data, recipe.decodedSize);
if (identifyModule(decoded, recipe).kind !== 'official') throw Error('测试源必须是支持的官方模块状态，请提供兼容的官方包副本。');
const target = path.join(root, 'research', 'settingmanager.decoded.bin');
fs.mkdirSync(path.dirname(target), { recursive: true });
fs.writeFileSync(target, decoded);
console.log('Local official test fixture prepared (excluded from Git).');
