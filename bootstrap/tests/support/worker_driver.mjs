// Starts workerd once with the Worker in `worker.mjs` and every module in the
// folder it is given, sends one request for each, and writes one line of JSON
// for each to its standard output: the name, the status of the response, the
// status the program ended with, and the body in base64.
import { Miniflare } from "miniflare";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const folder = process.argv[2];
const names = fs
  .readdirSync(folder)
  .filter((file) => file.endsWith(".wasm"))
  .map((file) => file.slice(0, -".wasm".length))
  .sort();
const mf = new Miniflare({
  compatibilityDate: "2025-01-01",
  modules: [
    { type: "ESModule", path: "worker.mjs", contents: fs.readFileSync(path.join(here, "worker.mjs"), "utf8") },
    ...names.map((name) => ({
      type: "CompiledWasm",
      path: `${name}.wasm`,
      contents: fs.readFileSync(path.join(folder, `${name}.wasm`)),
    })),
  ],
});
try {
  for (const name of names) {
    const response = await mf.dispatchFetch(`http://smith.test/${name}`);
    const body = Buffer.from(await response.arrayBuffer()).toString("base64");
    console.log([name, response.status, response.headers.get("x-smith-status"), body].join("\t"));
  }
} finally {
  await mf.dispose();
}
