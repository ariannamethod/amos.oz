#!/usr/bin/env node
/**
 * Headless selftest for amosOZ HTML kernel (v0.4.0).
 * Extracts <script> kernel from amosoz.html, runs in vm with browser stubs.
 */
import fs from "fs";
import path from "path";
import vm from "vm";
import { fileURLToPath } from "url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const htmlPath = path.join(__dirname, "..", "reffs", "amosoz.html");
const html = fs.readFileSync(htmlPath, "utf8");

/* An end tag closes the script even when it carries trailing garbage — `</script foo>`
 * is a close, `</scriptfoo>` is not. \s* was too narrow and let content hide past it. */
const match = html.match(/<script\b[^>]*>([\s\S]*?)<\/script\b[^>]*>/i);
if (!match) {
  console.error("html_selftest: no <script> block in amosoz.html");
  process.exit(1);
}

let script = match[1];
const uiMarker = "// ─── Terminal UI";
const uiIdx = script.indexOf(uiMarker);
if (uiIdx >= 0) script = script.slice(0, uiIdx);

const storage = { _d: {} };
const sandbox = {
  console,
  Date,
  Math,
  JSON,
  parseInt,
  String,
  Object,
  Array,
  Error,
  navigator: {
    userAgent: "node-html-selftest",
    platform: "Node",
    hardwareConcurrency: 4,
    language: "en",
  },
  window: { isSecureContext: true, screen: { width: 1920, height: 1080 } },
  screen: { width: 1920, height: 1080 },
  document: { getElementById: () => null, addEventListener: () => {} },
  localStorage: {
    setItem(k, v) { storage._d[k] = String(v); },
    getItem(k) { return k in storage._d ? storage._d[k] : null; },
    removeItem(k) { delete storage._d[k]; },
  },
};

vm.createContext(sandbox);
// Node vm does not hoist top-level class/const onto the sandbox object.
vm.runInContext(
  script + "\n;globalThis.__amosoz_exports__ = { AmosOZKernel, VERSION, SYSTEM_NAME, MOTD, BUILD_DATE };",
  sandbox
);

const exports = sandbox.__amosoz_exports__;
if (!exports || typeof exports.AmosOZKernel !== "function") {
  console.error("html_selftest: AmosOZKernel not defined after eval");
  process.exit(1);
}

const kernel = new exports.AmosOZKernel();
const output = kernel.dispatch("selftest");
console.log(output);

const header = output.match(/amosOZ Selftest \((\d+)\/(\d+) passed\)/);
if (!header) {
  console.error("html_selftest: could not parse selftest summary");
  process.exit(1);
}

const passed = parseInt(header[1], 10);
const total = parseInt(header[2], 10);
const allPassed = passed === 43 && total === 43 && output.includes("ALL TESTS PASSED");

if (allPassed) {
  console.log("html_selftest: 43/43 PASSED");
  process.exit(0);
}

console.error(`html_selftest: FAIL (${passed}/${total})`);
process.exit(1);