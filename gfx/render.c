#include "render.h"
#include "gl.h"
#include "shaders.h"

#define BLOOM_LEVELS 8
#define ENV_SIZE 256
#define ENV_LODS 5

typedef struct {
  u32 fbo, tex;
  int w, h;
} Target;

static struct {
  u32 scene, luma, down, up, post, room, prefilter;
  u32 env;
  u32 empty_vao;
  int w, h, rw, rh;
  float scale;
  int msaa;
  u32 ms_fbo, ms_color, ms_depth;
  Target hdr;
  Target luma_t;
  Target mips_down[BLOOM_LEVELS], mips_up[BLOOM_LEVELS];
  int nlevels;
  // Cached uniform locations of the scene shader.
  struct {
    int vp, model, tint, eye, mode, color, emissive, glow, metal, rough, opacity, clearcoat, lods, uvs, map, hasmap, env;
    int keydir, keycol, rimdir, rimcol, sky, ground, ppos, pcol, pdist, pdecay;
    int flowa, flowb, level, speed, phase, length, wiremode;
  } u;
} R;

PostParams post_defaults(void) {
  return (PostParams){.bloom = 1.1f,
                      .threshold = 0.62f,
                      .grain = 0.08f,
                      .vignette = 0.62f,
                      .contrast = 0.048f,
                      .brightness = 0,
                      .saturation = 0.028f,
                      .hue_deg = 0,
                      .chromatic = 0.000605f,
                      .fade = 0,
                      .time = 0};
}

static u32 compile(GLenum type, const char *src, char *err, size_t errlen) {
  u32 s = glCreateShader(type);
  const char *parts[2] = {GLSL_HEAD, src};
  glShaderSource(s, 2, parts, NULL);
  glCompileShader(s);
  int ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048] = "";
    glGetShaderInfoLog(s, sizeof log, NULL, log);
    if (err) snprintf(err, errlen, "shader: %s", log);
    glDeleteShader(s);
    return 0;
  }
  return s;
}

static u32 program(const char *vs, const char *fs, char *err, size_t errlen) {
  u32 v = compile(GL_VERTEX_SHADER, vs, err, errlen), f = v ? compile(GL_FRAGMENT_SHADER, fs, err, errlen) : 0;
  if (!v || !f) return 0;
  u32 p = glCreateProgram();
  glAttachShader(p, v);
  glAttachShader(p, f);
  glLinkProgram(p);
  glDeleteShader(v);
  glDeleteShader(f);
  int ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1024] = "";
    glGetProgramInfoLog(p, sizeof log, NULL, log);
    if (err) snprintf(err, errlen, "link: %s", log);
    return 0;
  }
  return p;
}
#define LOC(p, n) glGetUniformLocation((p), (n))

// ------------------------------------------------------------------ the room, into a cube
static void box_model(float px, float py, float pz, float ry, float sx, float sy, float sz, float out[16]) {
  Mat4 m = m4mul(m4mul(m4translate(v3(px, py - 3.5f, pz)), m4rot_y(ry)), m4scale(v3(sx, sy, sz)));
  memcpy(out, m.m, sizeof m.m);
}

static void draw_room(u32 vao, const Mat4 *vp) {
  glBindVertexArray(vao);
  glUniformMatrix4fv(LOC(R.room, "uViewProj"), 1, GL_FALSE, vp->m);
  float m[16];
#define BOX(px, py, pz, ry, sx, sy, sz, emit, flip)                 \
  do {                                                              \
    box_model(px, py, pz, ry, sx, sy, sz, m);                       \
    glUniformMatrix4fv(LOC(R.room, "uModel"), 1, GL_FALSE, m);      \
    glUniform1f(LOC(R.room, "uEmit"), emit);                        \
    glUniform1f(LOC(R.room, "uFlip"), flip);                        \
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);           \
  } while (0)
  BOX(-0.757f, 13.219f, 0.717f, 0, 31.713f, 28.305f, 28.591f, 0, -1);
  BOX(-10.906f, 2.009f, 1.846f, -0.195f, 2.328f, 7.905f, 4.651f, 0, 1);
  BOX(-5.607f, -0.754f, -0.758f, 0.994f, 1.970f, 1.534f, 3.955f, 0, 1);
  BOX(6.167f, 0.857f, 7.803f, 0.561f, 3.927f, 6.285f, 3.687f, 0, 1);
  BOX(-2.017f, 0.018f, 6.124f, 0.333f, 2.002f, 4.566f, 2.064f, 0, 1);
  BOX(2.291f, -0.756f, -2.621f, -0.286f, 1.546f, 1.552f, 1.496f, 0, 1);
  BOX(-2.193f, -0.369f, -5.547f, 0.516f, 3.875f, 3.487f, 2.986f, 0, 1);
  BOX(-16.116f, 14.37f, 8.208f, 0, 0.1f, 2.428f, 2.739f, 50, 1);
  BOX(-16.109f, 18.021f, -8.207f, 0, 0.1f, 2.425f, 2.751f, 50, 1);
  BOX(14.904f, 12.198f, -1.832f, 0, 0.15f, 4.265f, 6.331f, 17, 1);
  BOX(-0.462f, 8.89f, 14.520f, 0, 4.38f, 5.441f, 0.088f, 43, 1);
  BOX(3.235f, 11.486f, -12.541f, 0, 2.5f, 2.0f, 0.1f, 20, 1);
  BOX(0, 20, 0, 0, 1, 0.1f, 1, 100, 1);
#undef BOX
}

static void build_env(char *err, size_t errlen) {
  // A unit box.
  Mesh *box = mesh_box(1, 1, 1);
  u32 vao, vbo, ibo;
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glGenBuffers(1, &ibo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(Vertex) * (size_t)arr_len(box->v)), box->v, GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(sizeof(u32) * (size_t)arr_len(box->idx)), box->idx, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, px));
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, nx));
  mesh_free(box);

  u32 src, depth, fbo;
  glGenTextures(1, &src);
  glBindTexture(GL_TEXTURE_CUBE_MAP, src);
  int mips = 1;
  for (int s = ENV_SIZE; s > 1; s >>= 1) mips++;
  glTexStorage2D(GL_TEXTURE_CUBE_MAP, mips, GL_RGBA16F, ENV_SIZE, ENV_SIZE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glGenRenderbuffers(1, &depth);
  glBindRenderbuffer(GL_RENDERBUFFER, depth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, ENV_SIZE, ENV_SIZE);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
  glViewport(0, 0, ENV_SIZE, ENV_SIZE);
  glUseProgram(R.room);
  // The main light sits in the room, which is shifted down as a whole.
  glUniform3f(LOC(R.room, "uLightPos"), 0.418f, 16.199f - 3.5f, 0.300f);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  Vec3 dirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  Vec3 ups[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
  Mat4 proj = m4perspective(PI / 2, 1, 0.1f, 1000);
  for (int f = 0; f < 6; f++) {
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, (GLenum)(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f), src, 0);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Mat4 vp = m4mul(proj, m4lookat(v3(0, 0, 0), dirs[f], ups[f]));
    draw_room(vao, &vp);
  }
  glBindTexture(GL_TEXTURE_CUBE_MAP, src);
  glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

  // Pre-blur a chain of roughness levels.
  glGenTextures(1, &R.env);
  glBindTexture(GL_TEXTURE_CUBE_MAP, R.env);
  glTexStorage2D(GL_TEXTURE_CUBE_MAP, ENV_LODS + 1, GL_RGBA16F, ENV_SIZE, ENV_SIZE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
  glDisable(GL_DEPTH_TEST);
  glUseProgram(R.prefilter);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_CUBE_MAP, src);
  glUniform1i(LOC(R.prefilter, "uSrc"), 0);
  glUniform1f(LOC(R.prefilter, "uSrcSize"), ENV_SIZE);
  glBindVertexArray(R.empty_vao);
  for (int lod = 0; lod <= ENV_LODS; lod++) {
    int s = ENV_SIZE >> lod;
    glViewport(0, 0, s, s);
    glUniform1f(LOC(R.prefilter, "uRough"), (float)lod / ENV_LODS);
    for (int f = 0; f < 6; f++) {
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, (GLenum)(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f), R.env, lod);
      glUniform1i(LOC(R.prefilter, "uFace"), f);
      glDrawArrays(GL_TRIANGLES, 0, 3);
    }
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fbo);
  glDeleteRenderbuffers(1, &depth);
  glDeleteTextures(1, &src);
  glDeleteVertexArrays(1, &vao);
  glDeleteBuffers(1, &vbo);
  glDeleteBuffers(1, &ibo);
  (void)err;
  (void)errlen;
}

bool r_init(char *err, size_t errlen) {
  gl_extensions();
  glGenVertexArrays(1, &R.empty_vao);
  if (!(R.scene = program((const char *)SH_VS_SCENE, (const char *)SH_FS_SCENE, err, errlen))) return false;
  if (!(R.luma = program((const char *)SH_VS_FULL, (const char *)SH_FS_LUMA, err, errlen))) return false;
  if (!(R.down = program((const char *)SH_VS_FULL, (const char *)SH_FS_DOWN, err, errlen))) return false;
  if (!(R.up = program((const char *)SH_VS_FULL, (const char *)SH_FS_UP, err, errlen))) return false;
  if (!(R.post = program((const char *)SH_VS_FULL, (const char *)SH_FS_POST, err, errlen))) return false;
  if (!(R.room = program((const char *)SH_VS_ROOM, (const char *)SH_FS_ROOM, err, errlen))) return false;
  if (!(R.prefilter = program((const char *)SH_VS_CUBE, (const char *)SH_FS_PREFILTER, err, errlen))) return false;
#define U(f, n) R.u.f = LOC(R.scene, n)
  U(vp, "uViewProj"), U(model, "uModel"), U(tint, "uTintCopy"), U(eye, "uEye"), U(mode, "uMode"), U(color, "uColor");
  U(emissive, "uEmissive"), U(glow, "uGlow"), U(metal, "uMetal"), U(rough, "uRough"), U(opacity, "uOpacity");
  U(clearcoat, "uClearcoat"), U(lods, "uEnvLods"), U(uvs, "uUvScale"), U(map, "uMap"), U(hasmap, "uHasMap"), U(env, "uEnv");
  U(keydir, "uKeyDir"), U(keycol, "uKeyColor"), U(rimdir, "uRimDir"), U(rimcol, "uRimColor"), U(sky, "uHemiSky");
  U(ground, "uHemiGround"), U(ppos, "uPointPos"), U(pcol, "uPointColor"), U(pdist, "uPointDist"), U(pdecay, "uPointDecay");
  U(flowa, "uFlowA"), U(flowb, "uFlowB"), U(level, "uLevel"), U(speed, "uSpeed"), U(phase, "uPhase"), U(length, "uLength");
  U(wiremode, "uWireMode");
#undef U
  build_env(err, errlen);
  return glGetError() == GL_NO_ERROR || true;
}

void r_shutdown(void) {}

static void target_free(Target *t) {
  if (t->fbo) glDeleteFramebuffers(1, &t->fbo);
  if (t->tex) glDeleteTextures(1, &t->tex);
  memset(t, 0, sizeof *t);
}
static void target_make(Target *t, int w, int h) {
  target_free(t);
  t->w = w, t->h = h;
  glGenTextures(1, &t->tex);
  glBindTexture(GL_TEXTURE_2D, t->tex);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16F, w, h);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenFramebuffers(1, &t->fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->tex, 0);
}

void r_set_size(int w, int h, float scale, int msaa) {
  w = maxi(w, 1), h = maxi(h, 1);
  int rw = maxi((int)lroundf((float)w * scale), 1), rh = maxi((int)lroundf((float)h * scale), 1);
  if (rw == R.rw && rh == R.rh && msaa == R.msaa && w == R.w && h == R.h) return;
  R.w = w, R.h = h, R.rw = rw, R.rh = rh, R.scale = scale, R.msaa = msaa;
  target_make(&R.hdr, rw, rh);
  if (R.ms_fbo) {
    glDeleteFramebuffers(1, &R.ms_fbo);
    glDeleteRenderbuffers(1, &R.ms_color);
    glDeleteRenderbuffers(1, &R.ms_depth);
  }
  glGenRenderbuffers(1, &R.ms_color);
  glGenRenderbuffers(1, &R.ms_depth);
  glBindRenderbuffer(GL_RENDERBUFFER, R.ms_color);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaa, GL_RGBA16F, rw, rh);
  glBindRenderbuffer(GL_RENDERBUFFER, R.ms_depth);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaa, GL_DEPTH_COMPONENT24, rw, rh);
  glGenFramebuffers(1, &R.ms_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, R.ms_fbo);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, R.ms_color);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, R.ms_depth);
  // Bloom works from half size down, from the output size so it looks alike at any supersampling.
  target_make(&R.luma_t, maxi(w / 2, 1), maxi(h / 2, 1));
  int bw = R.luma_t.w, bh = R.luma_t.h;
  R.nlevels = 0;
  for (int i = 0; i < BLOOM_LEVELS; i++) {
    bw = (int)lroundf((float)bw * 0.5f), bh = (int)lroundf((float)bh * 0.5f);
    if (bw < 2 || bh < 2) {
      target_free(&R.mips_down[i]);
      target_free(&R.mips_up[i]);
      continue;
    }
    target_make(&R.mips_down[i], bw, bh);
    target_make(&R.mips_up[i], bw, bh);
    R.nlevels = i + 1;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

Mat4 camera_view_proj(const Camera *c, float aspect) {
  return m4mul(m4perspective(c->fov_deg * PI / 180, aspect, c->near_z, c->far_z), m4lookat(c->pos, c->target, v3(0, 1, 0)));
}

void camera_ray(const Camera *c, float aspect, float x, float y, float w, float h, Vec3 *origin, Vec3 *dir) {
  Vec3 f = v3norm(v3sub(c->target, c->pos)), s = v3norm(v3cross(f, v3(0, 1, 0))), u = v3cross(s, f);
  float t = tanf(c->fov_deg * PI / 360);
  float nx = (x / w) * 2 - 1, ny = 1 - (y / h) * 2;
  *origin = c->pos;
  *dir = v3norm(v3add(v3add(f, v3scale(s, nx * t * aspect)), v3scale(u, ny * t)));
}

bool camera_project(const Camera *c, float aspect, Vec3 p, float w, float h, float *sx, float *sy) {
  Vec4 q = m4vec4(camera_view_proj(c, aspect), p);
  if (q.w <= 0) return false;
  *sx = (q.x / q.w + 1) * 0.5f * w;
  *sy = (1 - q.y / q.w) * 0.5f * h;
  return true;
}

// ------------------------------------------------------------------ uploads
static void ensure_mesh(Mesh *g) {
  if (!g->gl_vao) {
    glGenVertexArrays(1, &g->gl_vao);
    glGenBuffers(1, &g->gl_vbo);
    glBindVertexArray(g->gl_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g->gl_vbo);
    if (g->idx) {
      glGenBuffers(1, &g->gl_ibo);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g->gl_ibo);
    }
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, px));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, nx));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, u));
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, along));
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, lane));
    g->gl_dirty = true;
  }
  if (g->gl_dirty) {
    glBindVertexArray(g->gl_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g->gl_vbo);
    size_t size = sizeof(Vertex) * (size_t)arr_len(g->v);
    if (g->gl_vcount == arr_len(g->v) && g->gl_vcount) {
      glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)size, g->v);
    } else {
      glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)size, g->v, GL_DYNAMIC_DRAW);
    }
    g->gl_vcount = arr_len(g->v);
    if (g->idx && g->gl_icount != arr_len(g->idx)) {
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g->gl_ibo);
      glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(sizeof(u32) * (size_t)arr_len(g->idx)), g->idx, GL_STATIC_DRAW);
      g->gl_icount = arr_len(g->idx);
    }
    g->gl_dirty = false;
  }
}

static void ensure_texture(Texture *t) {
  if (t->gl) return;
  glGenTextures(1, &t->gl);
  glBindTexture(GL_TEXTURE_2D, t->gl);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  // sRGB: mips and filtering happen in linear light, so fine patterns average to the right colour.
  glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, t->w, t->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, t->rgba);
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  GLenum wrap = t->repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (int)wrap);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (int)wrap);
  float aniso = 1;
  glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &aniso);
  if (aniso > 1 && glGetError() == GL_NO_ERROR) glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, minf(aniso, 8));
  free(t->rgba);
  t->rgba = NULL;
}

static void bind_instances(Node *n) {
  glBindVertexArray(n->mesh->gl_vao);
  if (n->inst) {
    if (!n->gl_inst) {
      glGenBuffers(1, &n->gl_inst);
      n->inst_dirty = true;
    }
    glBindBuffer(GL_ARRAY_BUFFER, n->gl_inst);
    if (n->inst_dirty) {
      glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(Mat4) * (size_t)arr_len(n->inst)), n->inst, GL_STATIC_DRAW);
      n->inst_dirty = false;
    }
    for (int i = 0; i < 4; i++) {
      glEnableVertexAttribArray((GLuint)(5 + i));
      glVertexAttribPointer((GLuint)(5 + i), 4, GL_FLOAT, GL_FALSE, sizeof(Mat4), (void *)(size_t)(i * 16));
      glVertexAttribDivisor((GLuint)(5 + i), 1);
    }
  } else {
    for (int i = 0; i < 4; i++) {
      glDisableVertexAttribArray((GLuint)(5 + i));
      float c[4] = {i == 0, i == 1, i == 2, i == 3};
      glVertexAttrib4fv((GLuint)(5 + i), c);
    }
  }
}

void r_release_model(Model *m) {
  for (int i = 0; i < arr_len(m->nodes); i++) {
    Node *n = &m->nodes[i];
    if (n->gl_inst) glDeleteBuffers(1, &n->gl_inst);
    n->gl_inst = 0;
    if (n->mesh && n->mesh->gl_vao) {
      glDeleteVertexArrays(1, &n->mesh->gl_vao);
      glDeleteBuffers(1, &n->mesh->gl_vbo);
      if (n->mesh->gl_ibo) glDeleteBuffers(1, &n->mesh->gl_ibo);
      n->mesh->gl_vao = 0;
    }
  }
}

// ------------------------------------------------------------------ drawing
typedef struct {
  int node, group;
  float dist;
  float opacity;
  bool highlight;
} DrawItem;

static int cmp_far_first(const void *a, const void *b) {
  float x = ((const DrawItem *)a)->dist, y = ((const DrawItem *)b)->dist;
  return x < y ? 1 : x > y ? -1 : 0;
}

static void set_vec(int loc, Vec3 v) { glUniform3f(loc, v.x, v.y, v.z); }

static void draw_item(Model *m, const DrawItem *it, const SceneHints *hints, Vec3 accent) {
  Node *n = &m->nodes[it->node];
  Mesh *g = n->mesh;
  ensure_mesh(g);
  bind_instances(n);
  int matno = g->ngroups ? mini(g->groups[it->group].mat, n->nmats - 1) : 0;
  if (n->nmats == 6) matno = it->group;
  const Material *mat = &n->mats[matno];
  glUniform1i(R.u.mode, (int)mat->mode);
  glUniformMatrix4fv(R.u.model, 1, GL_FALSE, n->world.m);
  set_vec(R.u.color, mat->color);
  Vec3 e = v3scale(mat->emissive, mat->emissive_intensity);
  if (n->glow_anim > 0) e = v3add(e, v3scale(n->glow_anim_color, n->glow_anim));
  if (it->highlight) {
    Vec3 mixed = v3lerp(mat->emissive, accent, 0.25f);
    e = v3scale(mixed, maxf(mat->emissive_intensity, 0.12f));
  }
  set_vec(R.u.emissive, e);
  set_vec(R.u.glow, mat->glow);
  glUniform1f(R.u.metal, mat->metalness);
  glUniform1f(R.u.rough, mat->roughness);
  glUniform1f(R.u.opacity, mat->opacity * it->opacity);
  glUniform1f(R.u.clearcoat, mat->clearcoat);
  glUniform1f(R.u.uvs, mat->uv_scale);
  glUniform1f(R.u.flowa, n->flow_a);
  glUniform1f(R.u.flowb, n->flow_b);
  glUniform1f(R.u.level, n->level_now);
  glUniform1f(R.u.speed, m->flow_speed);
  glUniform1f(R.u.phase, mat->wire_phase);
  glUniform1f(R.u.length, mat->wire_length);
  glUniform1i(R.u.wiremode, mat->wire_mode);
  glUniform1i(R.u.tint, hints && hints->hover_node == it->node ? hints->hover_copy : -1);
  if (mat->tex) {
    ensure_texture(mat->tex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, mat->tex->gl);
    glUniform1i(R.u.hasmap, 1);
  } else {
    glUniform1i(R.u.hasmap, 0);
  }
  if (mat->mode == MAT_FLOW) glDisable(GL_CULL_FACE);
  else glEnable(GL_CULL_FACE);
  bool blended = mat->transparent || it->opacity < 0.999f;
  if (blended) {
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
  } else {
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
  }
  if (mat->mode == MAT_DECAL) {
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1, -1);
  } else {
    glDisable(GL_POLYGON_OFFSET_FILL);
  }
  int start = 0, count = g->idx ? arr_len(g->idx) : arr_len(g->v);
  if (g->ngroups) start = g->groups[it->group].start, count = g->groups[it->group].count;
  int copies = n->inst ? arr_len(n->inst) : 1;
  if (g->idx) glDrawElementsInstanced(GL_TRIANGLES, count, GL_UNSIGNED_INT, (void *)(size_t)((size_t)start * sizeof(u32)), copies);
  else glDrawArraysInstanced(GL_TRIANGLES, start, count, copies);
}

static void scene_uniforms(const Model *m, const Camera *c, const Mat4 *vp) {
  glUseProgram(R.scene);
  glUniformMatrix4fv(R.u.vp, 1, GL_FALSE, vp->m);
  set_vec(R.u.eye, c->pos);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_CUBE_MAP, R.env);
  glUniform1i(R.u.env, 0);
  glUniform1i(R.u.map, 1);
  glUniform1f(R.u.lods, ENV_LODS);
  // The light rig, turned to sit relative to our default view and scaled to the model.
  Vec3 view = v3norm(v3(1, 0.7f, 1.2f));
  float turn = atan2f(view.x, view.z) - atan2f(-30, 40);
  float s = m->radius / 17, ct = cosf(turn), st = sinf(turn);
#define RIG(x, y, z) v3add(m->center, v3scale(v3((x)*ct + (z)*st, (y), -(x)*st + (z)*ct), s))
  Vec3 key = RIG(20.f, 40.f, 25.f), rim = RIG(-30.f, 10.f, -30.f), under = RIG(0.f, -6.f, 6.f);
#undef RIG
  set_vec(R.u.keydir, v3norm(v3sub(key, m->center)));
  set_vec(R.u.keycol, v3scale(hex_linear("#ffffff"), 2.2f));
  set_vec(R.u.rimdir, v3norm(v3sub(rim, m->center)));
  set_vec(R.u.rimcol, v3scale(hex_linear("#76b900"), 1.8f));
  set_vec(R.u.sky, v3scale(hex_linear("#dfeee0"), 0.35f));
  set_vec(R.u.ground, v3scale(hex_linear("#050806"), 0.35f));
  set_vec(R.u.ppos, under);
  set_vec(R.u.pcol, v3scale(hex_linear("#9cff3a"), 30 * powf(s, 1.6f)));
  glUniform1f(R.u.pdist, 60 * s);
  glUniform1f(R.u.pdecay, 1.6f);
}

static void fullscreen(void) {
  glBindVertexArray(R.empty_vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
}

static void post_chain(const PostParams *p, u32 dest_fbo) {
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glDisable(GL_POLYGON_OFFSET_FILL);
  glDepthMask(GL_TRUE);
  glActiveTexture(GL_TEXTURE0);
  // Bright parts.
  glBindFramebuffer(GL_FRAMEBUFFER, R.luma_t.fbo);
  glViewport(0, 0, R.luma_t.w, R.luma_t.h);
  glUseProgram(R.luma);
  glBindTexture(GL_TEXTURE_2D, R.hdr.tex);
  glUniform1i(LOC(R.luma, "uTex"), 0);
  glUniform1f(LOC(R.luma, "uThreshold"), p->threshold);
  glUniform1f(LOC(R.luma, "uSmoothing"), 0.25f);
  fullscreen();
  // Down the chain.
  glUseProgram(R.down);
  glUniform1i(LOC(R.down, "uTex"), 0);
  Target *prev = &R.luma_t;
  for (int i = 0; i < R.nlevels; i++) {
    Target *t = &R.mips_down[i];
    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    glViewport(0, 0, t->w, t->h);
    glBindTexture(GL_TEXTURE_2D, prev->tex);
    glUniform2f(LOC(R.down, "uTexel"), 1.0f / (float)prev->w, 1.0f / (float)prev->h);
    fullscreen();
    prev = t;
  }
  // And back up, each level mixed with the one above it.
  glUseProgram(R.up);
  glUniform1i(LOC(R.up, "uTex"), 0);
  glUniform1i(LOC(R.up, "uBase"), 1);
  glUniform1f(LOC(R.up, "uRadius"), 0.7f);
  for (int i = R.nlevels - 2; i >= 0; i--) {
    Target *src = i == R.nlevels - 2 ? &R.mips_down[R.nlevels - 1] : &R.mips_up[i + 1];
    Target *t = &R.mips_up[i];
    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    glViewport(0, 0, t->w, t->h);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, R.mips_down[i].tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src->tex);
    glUniform2f(LOC(R.up, "uTexel"), 1.0f / (float)src->w, 1.0f / (float)src->h);
    fullscreen();
  }
  // The final image.
  glBindFramebuffer(GL_FRAMEBUFFER, dest_fbo);
  glViewport(0, 0, R.w, R.h);
  glUseProgram(R.post);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, R.nlevels > 1 ? R.mips_up[0].tex : R.luma_t.tex);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, R.hdr.tex);
  glUniform1i(LOC(R.post, "uTex"), 0);
  glUniform1i(LOC(R.post, "uBloom"), 1);
  glUniform2f(LOC(R.post, "uChromatic"), p->chromatic, p->chromatic);
  float hr = p->hue_deg * PI / 180, hs = sinf(hr), hc = cosf(hr), rt3 = 1.7320508f;
  glUniform3f(LOC(R.post, "uHue"), (2 * hc + 1) / 3, (-rt3 * hs - hc + 1) / 3, (rt3 * hs - hc + 1) / 3);
  glUniform1f(LOC(R.post, "uBloomAmt"), p->bloom);
  glUniform1f(LOC(R.post, "uContrast"), p->contrast);
  glUniform1f(LOC(R.post, "uBrightness"), p->brightness);
  glUniform1f(LOC(R.post, "uSaturation"), p->saturation);
  glUniform1f(LOC(R.post, "uVignette"), p->vignette);
  glUniform1f(LOC(R.post, "uVigOffset"), 0.3f);
  glUniform1f(LOC(R.post, "uGrain"), p->grain);
  glUniform1f(LOC(R.post, "uTime"), p->time);
  glUniform1f(LOC(R.post, "uFade"), p->fade);
  fullscreen();
}

static void clear_hdr(void) {
  glBindFramebuffer(GL_FRAMEBUFFER, R.ms_fbo);
  glViewport(0, 0, R.rw, R.rh);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  Vec3 bg = hex_linear("#04070a");
  glClearColor(bg.x, bg.y, bg.z, 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void resolve_hdr(void) {
  glBindFramebuffer(GL_READ_FRAMEBUFFER, R.ms_fbo);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, R.hdr.fbo);
  glBlitFramebuffer(0, 0, R.rw, R.rh, 0, 0, R.rw, R.rh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

void r_render_empty(const PostParams *post, u32 dest_fbo) {
  clear_hdr();
  resolve_hdr();
  post_chain(post, dest_fbo);
}

void r_render(const Model *cm, const Camera *cam, const PostParams *post, const SceneHints *hints, u32 dest_fbo) {
  Model *m = (Model *)cm;
  clear_hdr();
  float aspect = (float)R.w / (float)R.h;
  Mat4 vp = camera_view_proj(cam, aspect);
  scene_uniforms(m, cam, &vp);
  glCullFace(GL_BACK);
  glFrontFace(GL_CCW);

  // Opaque in tree order; anything see-through after, far first.
  DrawItem *opaque = NULL, *clear = NULL;
  Vec3 accent = hex_linear("#76b900");
  int nn = arr_len(m->nodes);
  float *eff = malloc(sizeof(float) * (size_t)maxi(nn, 1));
  bool *hidden = malloc(sizeof(bool) * (size_t)maxi(nn, 1));
  bool *lit = malloc(sizeof(bool) * (size_t)maxi(nn, 1));
  for (int i = 0; i < nn; i++) {
    Node *n = &m->nodes[i];
    float parent = n->parent >= 0 ? eff[n->parent] : 1;
    eff[i] = parent * n->opacity;
    hidden[i] = (n->parent >= 0 && hidden[n->parent]) || (n->flags & NF_HIDDEN) || eff[i] <= 0.001f;
    bool self_lit = hints && hints->hover_node == i && hints->hover_copy < 0;
    lit[i] = (n->parent >= 0 && lit[n->parent]) || self_lit;
    if (hidden[i] || !n->mesh) continue;
    if ((n->flags & NF_WIRE) || (n->flags & NF_FLOW)) {
      // Nothing flowing is still something drawn.
    }
    int groups = n->mesh->ngroups ? n->mesh->ngroups : 1;
    Vec3 c = n->mesh ? m4point(n->world, box_center(n->mesh->box)) : n->home;
    float dist = v3dist(c, cam->pos);
    for (int g = 0; g < groups; g++) {
      int matno = n->mesh->ngroups ? mini(n->mesh->groups[g].mat, n->nmats - 1) : 0;
      if (n->nmats == 6) matno = g;
      bool blended = n->mats[matno].transparent || eff[i] < 0.999f;
      DrawItem it = {i, g, dist, eff[i], lit[i] && !(n->flags & NF_DECAL)};
      if (blended) arr_push(clear, it);
      else arr_push(opaque, it);
    }
  }
  for (int i = 0; i < arr_len(opaque); i++) draw_item(m, &opaque[i], hints, accent);
  if (clear) qsort(clear, (size_t)arr_len(clear), sizeof(DrawItem), cmp_far_first);
  for (int i = 0; i < arr_len(clear); i++) draw_item(m, &clear[i], hints, accent);
  arr_free(opaque);
  arr_free(clear);
  free(eff);
  free(hidden);
  free(lit);
  glBindVertexArray(0);
  glDepthMask(GL_TRUE);
  resolve_hdr();
  post_chain(post, dest_fbo);
}

u32 r_offscreen_fbo(int w, int h) {
  u32 fbo, tex;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  return fbo;
}

void r_read_rgba(u32 fbo, int w, int h, u8 *out) {
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, out);
  // GL's first row is the bottom.
  size_t row = (size_t)w * 4;
  u8 *tmp = malloc(row);
  for (int y = 0; y < h / 2; y++) {
    u8 *a = out + (size_t)y * row, *b = out + (size_t)(h - 1 - y) * row;
    memcpy(tmp, a, row);
    memcpy(a, b, row);
    memcpy(b, tmp, row);
  }
  free(tmp);
}
