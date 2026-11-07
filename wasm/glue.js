// Runs the engine in the page: hands it the data, forwards its GL calls to WebGL2, feeds it input.
(async function () {
  const canvas = document.getElementById("c");
  const fail = (msg) => {
    const f = document.getElementById("fail");
    f.textContent = msg;
    f.style.display = "flex";
    document.documentElement.dataset.error = msg;
  };
  const gl = canvas.getContext("webgl2", { antialias: false, alpha: false, depth: false, stencil: false, preserveDrawingBuffer: false, powerPreference: "high-performance" });
  if (!gl) return fail("This page needs WebGL2, which this browser does not offer.");

  const encoder = new TextEncoder(), decoder = new TextDecoder();
  const raw = JSON.parse(document.getElementById("data").textContent);
  const files = new Map();
  for (const k in raw.text) files.set(k, encoder.encode(raw.text[k]));
  for (const k in raw.bin) files.set(k, Uint8Array.from(atob(raw.bin[k]), (c) => c.charCodeAt(0)));

  let wasm, memory;
  const u8 = () => new Uint8Array(memory.buffer);
  const i32 = () => new Int32Array(memory.buffer);
  const f32 = () => new Float32Array(memory.buffer);
  const str = (p, n) => {
    const b = u8();
    if (n === undefined) {
      n = 0;
      while (b[p + n]) n++;
    }
    return decoder.decode(b.subarray(p, p + n));
  };
  const writeStr = (s, p, cap) => {
    const e = encoder.encode(s);
    const n = Math.min(e.length, cap - 1);
    u8().set(e.subarray(0, n), p);
    u8()[p + n] = 0;
    return n;
  };

  // GL objects are numbers on the C side.
  const T = { buf: [null], tex: [null], fb: [null], rb: [null], vao: [null], prog: [null], sh: [null], loc: [null] };
  const locCache = new Map();
  const gen = (table, make) => (n, p) => {
    for (let i = 0; i < n; i++) {
      table.push(make());
      i32()[(p >> 2) + i] = table.length - 1;
    }
  };
  const del = (table, kill) => (n, p) => {
    for (let i = 0; i < n; i++) {
      const id = i32()[(p >> 2) + i];
      if (table[id]) kill(table[id]);
      table[id] = null;
    }
  };
  const CH = { 0x1903: 1, 0x8227: 2, 0x1907: 3, 0x1908: 4 }; // RED RG RGB RGBA
  const BYTES = { 0x1401: 1, 0x140b: 2, 0x8d61: 2, 0x1406: 4 }; // UBYTE HALF_FLOAT HALF_FLOAT_OES FLOAT
  const pixels = (w, h, format, type, p) => {
    if (!p) return null;
    const n = w * h * (CH[format] || 4);
    if (type === 0x1406) return new Float32Array(memory.buffer, p, n);
    if (type === 0x140b || type === 0x8d61) return new Uint16Array(memory.buffer, p, n);
    return new Uint8Array(memory.buffer, p, n);
  };
  const GL = {
    glActiveTexture: (t) => gl.activeTexture(t),
    glAttachShader: (p, s) => gl.attachShader(T.prog[p], T.sh[s]),
    glBindBuffer: (t, b) => gl.bindBuffer(t, T.buf[b]),
    glBindFramebuffer: (t, f) => gl.bindFramebuffer(t, T.fb[f]),
    glBindRenderbuffer: (t, r) => gl.bindRenderbuffer(t, T.rb[r]),
    glBindTexture: (t, x) => gl.bindTexture(t, T.tex[x]),
    glBindVertexArray: (v) => gl.bindVertexArray(T.vao[v]),
    glBlendFuncSeparate: (a, b, c, d) => gl.blendFuncSeparate(a, b, c, d),
    glBlitFramebuffer: (a, b, c, d, e, f, g, h, m, x) => gl.blitFramebuffer(a, b, c, d, e, f, g, h, m, x),
    glBufferData: (t, n, p, u) => (p ? gl.bufferData(t, new Uint8Array(memory.buffer, p, n), u) : gl.bufferData(t, n, u)),
    glBufferSubData: (t, o, n, p) => gl.bufferSubData(t, o, new Uint8Array(memory.buffer, p, n)),
    glClear: (m) => gl.clear(m),
    glClearColor: (r, g, b, a) => gl.clearColor(r, g, b, a),
    glCompileShader: (s) => gl.compileShader(T.sh[s]),
    glCreateProgram: () => (T.prog.push(gl.createProgram()), T.prog.length - 1),
    glCreateShader: (t) => (T.sh.push(gl.createShader(t)), T.sh.length - 1),
    glCullFace: (m) => gl.cullFace(m),
    glDeleteBuffers: del(T.buf, (x) => gl.deleteBuffer(x)),
    glDeleteFramebuffers: del(T.fb, (x) => gl.deleteFramebuffer(x)),
    glDeleteRenderbuffers: del(T.rb, (x) => gl.deleteRenderbuffer(x)),
    glDeleteShader: (s) => gl.deleteShader(T.sh[s]),
    glDeleteTextures: del(T.tex, (x) => gl.deleteTexture(x)),
    glDeleteVertexArrays: del(T.vao, (x) => gl.deleteVertexArray(x)),
    glDepthMask: (f) => gl.depthMask(!!f),
    glDisable: (c) => gl.disable(c),
    glDisableVertexAttribArray: (i) => gl.disableVertexAttribArray(i),
    glDrawArrays: (m, f, n) => gl.drawArrays(m, f, n),
    glDrawArraysInstanced: (m, f, n, c) => gl.drawArraysInstanced(m, f, n, c),
    glDrawElements: (m, n, t, o) => gl.drawElements(m, n, t, o),
    glDrawElementsInstanced: (m, n, t, o, c) => gl.drawElementsInstanced(m, n, t, o, c),
    glEnable: (c) => gl.enable(c),
    glEnableVertexAttribArray: (i) => gl.enableVertexAttribArray(i),
    glFramebufferRenderbuffer: (t, a, r, b) => gl.framebufferRenderbuffer(t, a, r, T.rb[b]),
    glFramebufferTexture2D: (t, a, x, tex, l) => gl.framebufferTexture2D(t, a, x, T.tex[tex], l),
    glFrontFace: (m) => gl.frontFace(m),
    glGenBuffers: gen(T.buf, () => gl.createBuffer()),
    glGenerateMipmap: (t) => gl.generateMipmap(t),
    glGenFramebuffers: gen(T.fb, () => gl.createFramebuffer()),
    glGenRenderbuffers: gen(T.rb, () => gl.createRenderbuffer()),
    glGenTextures: gen(T.tex, () => gl.createTexture()),
    glGenVertexArrays: gen(T.vao, () => gl.createVertexArray()),
    glGetError: () => gl.getError(),
    glGetFloatv: (n, p) => {
      f32()[p >> 2] = Number(gl.getParameter(n)) || 0;
    },
    glGetFramebufferAttachmentParameteriv: (t, a, n, p) => {
      i32()[p >> 2] = 0;
    },
    glGetProgramInfoLog: (x, cap, lenp, out) => {
      const n = writeStr(gl.getProgramInfoLog(T.prog[x]) || "", out, cap);
      if (lenp) i32()[lenp >> 2] = n;
    },
    glGetProgramiv: (x, n, p) => {
      i32()[p >> 2] = n === 0x8b84 ? (gl.getProgramInfoLog(T.prog[x]) || "").length + 1 : Number(gl.getProgramParameter(T.prog[x], n));
    },
    glGetShaderInfoLog: (x, cap, lenp, out) => {
      const n = writeStr(gl.getShaderInfoLog(T.sh[x]) || "", out, cap);
      if (lenp) i32()[lenp >> 2] = n;
    },
    glGetShaderiv: (x, n, p) => {
      i32()[p >> 2] = n === 0x8b84 ? (gl.getShaderInfoLog(T.sh[x]) || "").length + 1 : Number(gl.getShaderParameter(T.sh[x], n));
    },
    glGetUniformLocation: (x, p) => {
      const name = str(p), key = x + ":" + name;
      if (locCache.has(key)) return locCache.get(key);
      const l = gl.getUniformLocation(T.prog[x], name);
      let id = -1;
      if (l) {
        T.loc.push(l);
        id = T.loc.length - 1;
      }
      locCache.set(key, id);
      return id;
    },
    glLinkProgram: (x) => gl.linkProgram(T.prog[x]),
    glPixelStorei: (n, v) => gl.pixelStorei(n, v),
    glPolygonOffset: (a, b) => gl.polygonOffset(a, b),
    glReadPixels: (x, y, w, h, f, t, p) => gl.readPixels(x, y, w, h, f, t, pixels(w, h, f, t, p)),
    glRenderbufferStorage: (t, f, w, h) => gl.renderbufferStorage(t, f, w, h),
    glRenderbufferStorageMultisample: (t, s, f, w, h) => gl.renderbufferStorageMultisample(t, s, f, w, h),
    glScissor: (x, y, w, h) => gl.scissor(x, y, w, h),
    glShaderSource: (x, count, strs, lens) => {
      let src = "";
      for (let i = 0; i < count; i++) {
        const q = i32()[(strs >> 2) + i], l = lens ? i32()[(lens >> 2) + i] : -1;
        src += l >= 0 ? str(q, l) : str(q);
      }
      gl.shaderSource(T.sh[x], src);
    },
    glTexImage2D: (t, l, i, w, h, b, f, ty, p) => gl.texImage2D(t, l, i, w, h, b, f, ty, pixels(w, h, f, ty, p)),
    glTexParameterf: (t, n, v) => gl.texParameterf(t, n, v),
    glTexParameteri: (t, n, v) => gl.texParameteri(t, n, v),
    glTexStorage2D: (t, l, f, w, h) => gl.texStorage2D(t, l, f, w, h),
    glTexSubImage2D: (t, l, x, y, w, h, f, ty, p) => gl.texSubImage2D(t, l, x, y, w, h, f, ty, pixels(w, h, f, ty, p)),
    glUniform1f: (l, a) => gl.uniform1f(l < 0 ? null : T.loc[l], a),
    glUniform1i: (l, a) => gl.uniform1i(l < 0 ? null : T.loc[l], a),
    glUniform2f: (l, a, b) => gl.uniform2f(l < 0 ? null : T.loc[l], a, b),
    glUniform3f: (l, a, b, c) => gl.uniform3f(l < 0 ? null : T.loc[l], a, b, c),
    glUniformMatrix4fv: (l, n, tr, p) => gl.uniformMatrix4fv(l < 0 ? null : T.loc[l], false, new Float32Array(memory.buffer, p, 16 * n)),
    glUseProgram: (x) => gl.useProgram(T.prog[x]),
    glVertexAttrib4fv: (i, p) => gl.vertexAttrib4fv(i, new Float32Array(memory.buffer, p, 4)),
    glVertexAttribDivisor: (i, d) => gl.vertexAttribDivisor(i, d),
    glVertexAttribPointer: (i, n, t, norm, s, o) => gl.vertexAttribPointer(i, n, t, !!norm, s, o),
    glViewport: (x, y, w, h) => gl.viewport(x, y, w, h),
    gl_extensions: () => {
      gl.getExtension("EXT_color_buffer_float");
      gl.getExtension("EXT_color_buffer_half_float");
      gl.getExtension("OES_texture_float_linear");
      gl.getExtension("EXT_texture_filter_anisotropic");
    },
  };

  // C's number formats, from the browser's exact ones.
  const formatG = (v, prec) => {
    if (prec === 0) prec = 1;
    if (v === 0) return Object.is(v, -0) ? "-0" : "0";
    const e = Number(v.toExponential(prec - 1).split("e")[1]);
    let s;
    if (e < -4 || e >= prec) {
      let [m, x] = v.toExponential(prec - 1).split("e");
      if (m.includes(".")) m = m.replace(/\.?0+$/, "");
      const n = Number(x);
      s = m + "e" + (n < 0 ? "-" : "+") + String(Math.abs(n)).padStart(2, "0");
    } else {
      s = v.toFixed(prec - 1 - e);
      if (s.includes(".")) s = s.replace(/\.?0+$/, "");
    }
    return s;
  };
  const env = {
    ...GL,
    sin: Math.sin, cos: Math.cos, tan: Math.tan, atan: Math.atan, atan2: Math.atan2, acos: Math.acos, asin: Math.asin,
    pow: Math.pow, exp: Math.exp, log: Math.log, log2: Math.log2, log10: Math.log10,
    fmod: (a, b) => a % b,
    js_log: (p, n) => console.log(str(p, n)),
    js_abort: () => {
      throw new Error("the engine stopped");
    },
    js_format: (v, conv, prec, out, cap) => {
      let s;
      if (!Number.isFinite(v)) s = Number.isNaN(v) ? "nan" : v < 0 ? "-inf" : "inf";
      else if (conv === 102) s = v.toFixed(Math.min(prec, 100));
      else if (conv === 101) {
        const [m, x] = v.toExponential(Math.min(prec, 100)).split("e");
        const n = Number(x);
        s = m + "e" + (n < 0 ? "-" : "+") + String(Math.abs(n)).padStart(2, "0");
      } else s = formatG(v, Math.min(prec, 100));
      return writeStr(s, out, cap);
    },
    js_strtod: (p, usedp) => {
      const b = u8();
      let s = "";
      for (let i = 0; i < 64 && b[p + i]; i++) s += String.fromCharCode(b[p + i]);
      const m = /^[ \t\r\n]*[+-]?(?:\d+\.?\d*(?:[eE][+-]?\d+)?|\.\d+(?:[eE][+-]?\d+)?|inf(?:inity)?|nan)/i.exec(s);
      if (!m) {
        i32()[usedp >> 2] = 0;
        return 0;
      }
      i32()[usedp >> 2] = m[0].length;
      const t = m[0].trim().toLowerCase();
      if (t.endsWith("nan")) return NaN;
      if (t.includes("inf")) return t[0] === "-" ? -Infinity : Infinity;
      return parseFloat(t);
    },
    js_file_size: (p) => {
      const f = files.get(str(p));
      return f ? f.length : -1;
    },
    js_file_read: (p, out) => {
      u8().set(files.get(str(p)), out);
    },
  };

  const bytes = Uint8Array.from(atob(WASM_B64), (c) => c.charCodeAt(0));
  try {
    const { instance } = await WebAssembly.instantiate(bytes, { env });
    wasm = instance.exports;
    memory = wasm.memory;
  } catch (err) {
    return fail("This page could not start: " + err.message);
  }

  const buf = wasm.web_buf();
  const put = (s) => writeStr(s, buf, 4096);
  env.gl_extensions();
  put(ENTRY);
  if (!wasm.web_start()) return fail("This page could not open its model.");
  document.title = str(wasm.web_title()) || document.title;

  const resize = () => {
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const w = Math.max(1, Math.round(canvas.clientWidth * dpr)), h = Math.max(1, Math.round(canvas.clientHeight * dpr));
    if (canvas.width === w && canvas.height === h) return;
    canvas.width = w;
    canvas.height = h;
    wasm.web_resize(w, h, dpr);
  };
  const scale = () => canvas.width / canvas.clientWidth;
  const move = (e) => {
    const r = canvas.getBoundingClientRect(), k = scale();
    wasm.web_mouse((e.clientX - r.left) * k, (e.clientY - r.top) * k);
    wasm.web_mods(e.ctrlKey ? 1 : 0, e.shiftKey ? 1 : 0);
  };
  const BTN = [0, 2, 1]; // DOM: left, middle, right. Engine: left, right, middle.
  canvas.addEventListener("pointermove", move);
  canvas.addEventListener("pointerdown", (e) => {
    canvas.focus();
    canvas.setPointerCapture(e.pointerId);
    move(e);
    wasm.web_button(BTN[e.button] ?? 0, 1, e.detail === 2 ? 1 : 0);
    e.preventDefault();
  });
  canvas.addEventListener("pointerup", (e) => {
    move(e);
    wasm.web_button(BTN[e.button] ?? 0, 0, 0);
  });
  canvas.addEventListener("contextmenu", (e) => e.preventDefault());
  canvas.addEventListener("wheel", (e) => {
    move(e);
    const unit = e.deltaMode === 1 ? 3 : e.deltaMode === 2 ? 30 : 100;
    wasm.web_wheel(-e.deltaY / unit);
    e.preventDefault();
  }, { passive: false });
  window.addEventListener("keydown", (e) => {
    wasm.web_mods(e.ctrlKey ? 1 : 0, e.shiftKey ? 1 : 0);
    put(e.key);
    wasm.web_key();
    if (e.key.length === 1 && !e.ctrlKey && !e.metaKey) {
      put(e.key);
      wasm.web_text();
    }
    if (e.key === " " || e.key.startsWith("Arrow")) e.preventDefault();
  });
  canvas.addEventListener("webglcontextlost", (e) => {
    e.preventDefault();
    fail("The graphics context was lost. Reload the page.");
  });
  new ResizeObserver(resize).observe(canvas);
  resize();

  let last = performance.now(), frames = 0;
  const tick = (now) => {
    const dt = Math.min((now - last) / 1000, 0.1);
    last = now;
    resize();
    try {
      wasm.web_frame(dt);
    } catch (err) {
      return fail("This page stopped: " + err.message);
    }
    frames++;
    if (frames === 1 || frames % 10 === 0) {
      document.documentElement.dataset.frames = frames;
      const e = gl.getError();
      if (e) {
        document.documentElement.dataset.error = "GL error 0x" + e.toString(16);
        console.error(document.documentElement.dataset.error);
      }
    }
    requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);
})();
