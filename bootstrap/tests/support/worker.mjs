// The Worker that runs a module `smith build --target wasm` wrote. It is
// written for the test and is not the file beside the module, which is for
// `node`. A request to `/name` runs the module `name.wasm` and answers with
// what it printed.
//
// The status is 200 when the program ends with status 0, and 500 when it ends
// with another: the body is still what it printed before it ended, and the
// header `x-smith-status` says the status it ended with.

export default {
  async fetch(request) {
    const name = new URL(request.url).pathname.slice(1);
    // A Worker may not compile bytes (`WebAssembly.Module(bytes)` is refused),
    // so the module is one that Cloudflare compiled when it loaded the Worker,
    // and is imported by name, as a module of the type `CompiledWasm`.
    const { default: module } = await import(`./${name}.wasm`);
    let memory;
    const printed = [];
    const imports = {
      console: {
        // A Worker has no standard output: standard output is the body, and
        // standard error is the Worker's log.
        print(to, address, length) {
          const bytes = new Uint8Array(memory.buffer, address, length).slice();
          if (to === 1) printed.push(bytes);
          else console.error(new TextDecoder().decode(bytes));
        },
      },
    };
    // A new instance for each request, as `node` starts one for each run: the
    // memory of an instance outlives a request, and what it printed with it.
    const instance = new WebAssembly.Instance(module, imports);
    memory = instance.exports.memory;
    let status = 0;
    try {
      instance.exports.main();
    } catch (error) {
      status = instance.exports.status();
      if (status < 0) throw error;
    }
    return new Response(new Blob(printed), {
      status: status === 0 ? 200 : 500,
      headers: { "x-smith-status": String(status) },
    });
  },
};
