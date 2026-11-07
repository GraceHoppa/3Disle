// The engine's checks: runs without a screen. The golden boxes in tests/golden come from the
// original implementation, so geometry and layout stay what the data's authors saw.
#include "json.h"
#include "model.h"
#include "paint.h"
#include "spec.h"

static int g_fail, g_checks;
#define CHECK(c, ...)                                 \
  do {                                                \
    g_checks++;                                       \
    if (!(c)) {                                       \
      g_fail++;                                       \
      printf("FAIL %s:%d: ", __FILE__, __LINE__);     \
      printf(__VA_ARGS__);                            \
      printf("\n");                                   \
    }                                                 \
  } while (0)
#define NEAR(a, b, eps) (fabs((double)(a) - (double)(b)) <= (eps))

static void test_json(void) {
  Arena ar = {0};
  char err[128] = "";
  Json *j = json_parse(&ar, "{\"a\": [1, 2.5, {\"b\": \"x\\n\\u00e9\"}], \"t\": true, \"n\": null}", err, sizeof err);
  CHECK(j, "parse: %s", err);
  CHECK(json_get(j, "a")->count == 3, "array count");
  CHECK(!strcmp(json_str(json_get(j, "a")->tail, "b", ""), "x\n\xC3\xA9"), "escapes");
  char *text = json_write(j);
  Json *again = json_parse(&ar, text, err, sizeof err);
  CHECK(again && json_equal(j, again), "write/parse round trip");
  free(text);
  CHECK(!json_parse(&ar, "{\"a\": }", err, sizeof err), "bad json is refused");
  CHECK(strstr(err, "line 1"), "error names the line: %s", err);
  CHECK(!json_parse(&ar, "[1, 2", err, sizeof err), "unterminated array");
  CHECK(!json_parse(&ar, "\"abc", err, sizeof err), "unterminated string");
  CHECK(!json_parse(&ar, "1 2", err, sizeof err), "trailing text");
  arena_free(&ar);
}

static void test_logic(void) {
  Dbl2 *m = NULL;
  arr_push(m, ((Dbl2){0, 0}));
  arr_push(m, ((Dbl2){100, 30000}));
  CHECK(lookup(m, 50) == 15000, "lookup mid");
  CHECK(lookup(m, -5) == 0, "flat below");
  CHECK(lookup(m, 500) == 30000, "flat above");
  CHECK(lookup(NULL, 5) == 0, "empty table");
  CHECK(lookup(m, NAN) == 0, "nan");
  arr_free(m);
  char b[64];
  format_readout(b, sizeof b, 21400, "rpm", 0);
  CHECK(!strcmp(b, "21 400 rpm"), "%s", b);
  format_readout(b, sizeof b, 3.84, "A", 1);
  CHECK(!strcmp(b, "3.8 A"), "%s", b);
  format_readout(b, sizeof b, 999, NULL, 0);
  CHECK(!strcmp(b, "999"), "%s", b);
  format_readout(b, sizeof b, 1234567, NULL, 0);
  CHECK(!strcmp(b, "1 234 567"), "%s", b);
}

static const char *const MODELS[] = {
    "devices/pixel6/board.json", "devices/pixel6/device.json", "devices/pixel6/soc.json", "drone/battery.json",
    "drone/esc.json",            "drone/fc.json",              "drone/mcu.json",          "drone/motor.json",
    "drone/quad.json",           "rtx5070ti/card.json",        "rtx5070ti/die.json",      "rtx5070ti/package.json",
    "rtx5070ti/pcb.json",
};
#define DATA "public/models/"

// Saving writes back everything that was in the file, whatever we don't understand included.
static void test_round_trip(void) {
  for (int i = 0; i < COUNT(MODELS); i++) {
    char path[256], err[128] = "";
    snprintf(path, sizeof path, DATA "%s", MODELS[i]);
    size_t len;
    char *text = file_read(path, &len);
    CHECK(text, "read %s", path);
    if (!text) continue;
    AssemblySpec *s = spec_parse(text, err, sizeof err);
    CHECK(s, "%s: %s", path, err);
    if (!s) continue;
    char *saved = spec_write(s);
    Arena ar = {0};
    Json *a = json_parse(&ar, text, NULL, 0), *b = json_parse(&ar, saved, NULL, 0);
    CHECK(a && b && json_equal(a, b), "%s changes when saved", path);
    arena_free(&ar);
    free(saved);
    free(text);
    spec_free(s);
  }
}

// Every named node sits where the original implementation put it.
static void test_golden(void) {
  size_t len;
  char *gold = file_read("tests/golden/boxes.json", &len);
  CHECK(gold, "golden file");
  if (!gold) return;
  Arena ar = {0};
  Json *g = json_parse(&ar, gold, NULL, 0);
  free(gold);
  for (const Json *entry = g->head; entry; entry = entry->next) {
    char path[256], err[128] = "";
    snprintf(path, sizeof path, DATA "%s", entry->key);
    AssemblySpec *s = spec_load(path, err, sizeof err);
    CHECK(s, "%s: %s", path, err);
    if (!s) continue;
    Model *m = model_build(s, (BuildOpts){0});
    CHECK(!arr_len(m->missing), "%s: wires name nothing: %s", path, arr_len(m->missing) ? m->missing[0] : "");
    int want = entry->count, have = 0;
    for (int i = 1; i < arr_len(m->nodes); i++)
      if (m->nodes[i].name[0]) have++;
    CHECK(want == have, "%s: %d named nodes, want %d", path, have, want);
    const Json *e = entry->head;
    int bad = 0;
    for (int i = 1; i < arr_len(m->nodes) && e; i++) {
      const Node *n = &m->nodes[i];
      if (!n->name[0]) continue;
      const char *name = json_str(e, "n", "");
      CHECK(!strcmp(name, n->name), "%s: node %d is '%s', want '%s'", path, i, n->name, name);
      const Json *mn = json_get(e, "min"), *mx = json_get(e, "max");
      double w[6] = {mn->head->num, mn->head->next->num, mn->tail->num, mx->head->num, mx->head->next->num, mx->tail->num};
      double h[6] = {n->box.min.x, n->box.min.y, n->box.min.z, n->box.max.x, n->box.max.y, n->box.max.z};
      double eps = 0.002 + 0.0005 * fabs(m->radius);
      if (n->flags & NF_WIRE) eps = 0.15;
      for (int k = 0; k < 6 && bad < 8; k++)
        if (!NEAR(w[k], h[k], eps)) {
          CHECK(false, "%s '%s' bound %d: %.4f, want %.4f", path, name, k, h[k], w[k]);
          bad++;
          break;
        }
      int want_copies = (int)json_num(e, "c", 1);
      if (n->inst) CHECK(arr_len(n->inst) == want_copies, "%s '%s': %d copies, want %d", path, name, arr_len(n->inst), want_copies);
      e = e->next;
    }
    model_free(m);
    spec_free(s);
  }
  arena_free(&ar);
}

static void test_spread(void) {
  char err[128] = "";
  AssemblySpec *s = spec_load(DATA "drone/quad.json", err, sizeof err);
  Model *m = model_build(s, (BuildOpts){0});
  Vec3 centre = box_center(node_box_now(m, m->pieces[0]));
  model_set_explode(m, 1);
  model_update(m);
  CHECK(v3dist(centre, box_center(node_box_now(m, m->pieces[0]))) > 1, "a piece moves apart");
  int np = arr_len(m->pieces);
  float span = 0;
  for (int i = 0; i < np; i++) span = maxf(span, fabsf(m->nodes[m->pieces[i]].off_spread.y));
  CHECK(NEAR(span * 2, m->radius, m->radius * 0.01), "the whole spread equals the radius: %f vs %f", (double)(span * 2), (double)m->radius);
  // Wires follow their ends.
  bool moved = false;
  for (int i = 0; i < arr_len(m->wires); i++) moved = moved || v3dist(m->wires[i].a.last, m->wires[i].a.p0) > 0.1f;
  CHECK(moved, "wires re-lay when the pieces move");
  model_set_explode(m, 0);
  model_update(m);
  CHECK(v3dist(centre, box_center(node_box_now(m, m->pieces[0]))) < 1e-3f, "back together");
  // A control drives spin.
  int prop = -1;
  for (int i = 0; i < arr_len(m->nodes); i++)
    if (m->nodes[i].spin && prop < 0) prop = i;
  CHECK(prop >= 0, "the quad has spinning parts");
  model_tick(m, 1);
  float a = m->nodes[prop].spin_angle;
  CHECK(a != 0, "spins");
  model_set_value(m, "throttle", 100);
  model_tick(m, 1);
  CHECK(fabsf(m->nodes[prop].spin_angle - a) > fabsf(a), "more throttle spins faster");
  model_free(m);
  spec_free(s);
}

static void test_pick(void) {
  AssemblySpec *s = spec_load(DATA "rtx5070ti/pcb.json", NULL, 0);
  Model *m = model_build(s, (BuildOpts){0});
  int chip = model_find(m, "GB203 package substrate");
  CHECK(chip >= 0, "found");
  if (chip >= 0) {
    Vec3 c = box_center(m->nodes[chip].box);
    Hit h = model_pick(m, v3(c.x, c.y + 500, c.z), v3(0, -1, 0));
    CHECK(h.node >= 0, "hit something");
    Hit none = model_pick(m, v3(9999, 9999, 9999), v3(0, 1, 0));
    CHECK(none.node < 0, "a miss is a miss");
  }
  model_free(m);
  spec_free(s);
}

static void test_paint(void) {
  // The same seed paints the same pixels; every finish paints something.
  const char *kinds[] = {"pcb", "carbon", "brushed", "traces", "laminate"};
  for (int i = 0; i < COUNT(kinds); i++) {
    u8 *a = paint_surface(kinds[i], 128, 128, 2, 64, 64, NULL, 0, 7, NULL);
    u8 *b = paint_surface(kinds[i], 128, 128, 2, 64, 64, NULL, 0, 7, NULL);
    CHECK(!memcmp(a, b, 128 * 128 * 4), "%s is deterministic", kinds[i]);
    bool varied = false;
    for (int k = 4; k < 128 * 128 * 4 && !varied; k += 4) varied = memcmp(a, a + k, 3) != 0;
    CHECK(varied, "%s paints a pattern", kinds[i]);
    free(a);
    free(b);
  }
  const char *pats[] = {"sm-grid", "sram", "phy", "logic"};
  for (int i = 0; i < COUNT(pats); i++) free(paint_pattern(pats[i], 96, 64, NULL, 3, 2, 6));
  u8 *mark = paint_marking("MCU STM32F405 (LQFP)", 96, 96, false);
  int ink = 0;
  for (int k = 3; k < 96 * 96 * 4; k += 4) ink += mark[k] > 32;
  CHECK(ink > 50, "marking has ink: %d", ink);
  free(mark);
  int w, h;
  surface_size(240, 110, 4, &w, &h);
  CHECK(w == 960 && h == 440, "%d x %d", w, h);
  surface_size(4000, 100, 4, &w, &h);
  CHECK(w == 2048, "long side clamped: %d", w);
}

static void test_editing(void) {
  AssemblySpec *s = spec_new("Board", 100, 60);
  int l = spec_add_layer(s, -1, "PCB");
  CHECK(l == 0, "layer index");
  for (int i = 0; i < SHAPE_COUNT; i++) {
    if (i == SHAPE_FILE) continue;
    CHECK(spec_add_part(s, l, (Shape)i), "add shape %d", i);
  }
  CHECK(strcmp(s->layers[0].parts[0].name, s->layers[0].parts[1].name), "new parts get free names");
  spec_add_wire(s, s->layers[0].parts[0].name, s->layers[0].parts[1].name);
  Model *m = model_build(s, (BuildOpts){0});
  CHECK(!arr_len(m->missing), "new wire connects");
  CHECK(m->radius > 1, "builds");
  model_free(m);
  AssemblySpec *c = spec_clone(s);
  char *a = spec_write(s), *b = spec_write(c);
  CHECK(!strcmp(a, b), "clone saves identically");
  free(a);
  free(b);
  spec_free(c);
  spec_free(s);
  CHECK(!spec_parse("{\"name\": \"no layers\"}", NULL, 0), "not an assembly");
  // An empty document, and hostile numbers, still build.
  AssemblySpec *e = spec_parse("{\"layers\": []}", NULL, 0);
  Model *me = model_build(e, (BuildOpts){0});
  CHECK(me->radius > 0, "empty model");
  model_free(me);
  spec_free(e);
  AssemblySpec *w = spec_parse(
      "{\"footprint\":[0,0],\"layers\":[{\"name\":\"L\",\"thickness\":0,\"parts\":[{\"name\":\"a\",\"shape\":\"box\",\"size\":[0,0,0],"
      "\"repeat\":{\"count\":-4}},{\"name\":\"o\",\"shape\":\"outline\",\"points\":[[0,0]]},{\"name\":\"f\",\"shape\":\"fan\",\"blades\":1}],"
      "\"links\":[{\"from\":\"nothing\",\"to\":\"a\"}]}],\"wires\":[{\"name\":\"w\",\"from\":\"x\",\"to\":\"a\"}]}",
      NULL, 0);
  CHECK(w, "odd file parses");
  Model *mw = model_build(w, (BuildOpts){.paint = true});
  CHECK(arr_len(mw->missing) == 1, "missing wire end is reported");
  model_free(mw);
  spec_free(w);
}

// ------------------------------------------------------------------ model files
static u8 *small_glb(size_t *out) {
  // Two objects, a parent and its child, each one triangle, and the child moved up by a metre.
  const char *js = "{\"asset\":{\"version\":\"2.0\",\"extras\":{\"author\":\"A\",\"license\":\"L\"}},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
                   "\"nodes\":[{\"name\":\"Top\",\"children\":[1,2]},{\"name\":\"Base plate\",\"mesh\":0},{\"name\":\"Arm.1\",\"mesh\":0,\"translation\":[0,1,0]}],"
                   "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1,\"material\":0}]}],"
                   "\"materials\":[{\"name\":\"m\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,0,0,1],\"metallicFactor\":0,\"roughnessFactor\":0.5}}],"
                   "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
                   "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}],\"buffers\":[{\"byteLength\":44}]}";
  size_t jl = strlen(js), jp = (jl + 3) & ~(size_t)3, total = 12 + 8 + jp + 8 + 44;
  u8 *b = calloc(1, total);
  u32 h[3] = {0x46546c67, 2, (u32)total};
  memcpy(b, h, 12);
  u32 ch[2] = {(u32)jp, 0x4e4f534a};
  memcpy(b + 12, ch, 8);
  memset(b + 20, ' ', jp);
  memcpy(b + 20, js, jl);
  u32 ch2[2] = {44, 0x004e4942};
  memcpy(b + 20 + jp, ch2, 8);
  float pos[9] = {0, 0, 0, 1, 0, 0, 0, 0, 1};
  uint16_t ix[3] = {0, 1, 2};
  memcpy(b + 28 + jp, pos, 36);
  memcpy(b + 64 + jp, ix, 6);
  *out = total;
  return b;
}

static u32 fuzz_next(u32 *s) {
  *s = *s * 1664525u + 1013904223u;
  return *s >> 8;
}

// Whatever the bytes say, parsing stays inside them. Run under the sanitizers by `make asan`.
static void fuzz_bytes(const u8 *orig, size_t n, int rounds, size_t lo, size_t hi, u32 seed, int *parsed) {
  for (int r = 0; r < rounds; r++) {
    size_t len = r % 7 == 0 ? fuzz_next(&seed) % (n + 1) : n;
    u8 *c = malloc(n + 1);
    memcpy(c, orig, n);
    int flips = 1 + (int)(fuzz_next(&seed) % 12);
    for (int k = 0; k < flips && hi > lo; k++) {
      size_t at = lo + fuzz_next(&seed) % (hi - lo);
      if (at < n) c[at] = k & 1 ? (u8)fuzz_next(&seed) : (u8)("0123456789-.e\"[]{},"[fuzz_next(&seed) % 19]);
    }
    char err[256];
    ModelFile *f = modelfile_parse(c, len, "fuzz", err, sizeof err);
    if (f) {
      (*parsed)++;
      modelfile_free(f);
    }
  }
}

static void test_model_files(void) {
  char err[512] = "";
  size_t n;
  u8 *glb = small_glb(&n);
  u8 *copy = malloc(n);
  memcpy(copy, glb, n);
  ModelFile *f = modelfile_parse(copy, n, "small.glb", err, sizeof err);
  CHECK(f, "small GLB: %s", err);
  if (f) {
    CHECK(arr_len(f->objects) == 3 && arr_len(f->prims) == 2, "objects and primitives");
    CHECK(NEAR(f->box.max.y, 1, 1e-6), "the child's matrix is baked in");
    CHECK(f->units == 1000, "glTF is metres");
    CHECK(f->credit && strstr(f->credit, "A"), "credit comes from the file");
    const char *ask[] = {"Base_plate"}, *ask2[] = {"Arm1"}, *ask3[] = {"Top"}, *ask4[] = {"nope"};
    bool *on = modelfile_pick(f, ask, 1);
    CHECK(!on[0] && on[1] && !on[2], "picks by a rewritten name");
    arr_free(on);
    on = modelfile_pick(f, ask2, 1);
    CHECK(on[2] && !on[1], "dots are dropped the way three.js drops them");
    arr_free(on);
    on = modelfile_pick(f, ask3, 1);
    CHECK(on[0] && on[1] && on[2], "a parent brings its children");
    arr_free(on);
    on = modelfile_pick(f, ask4, 1);
    CHECK(!on[0] && !on[1] && !on[2], "an unknown name picks nothing");
    arr_free(on);
    int *cut = modelfile_natural_parts(f);
    CHECK(arr_len(cut) == 2, "cut where the file branches, got %d", arr_len(cut));
    arr_free(cut);
    modelfile_free(f);
  }
  int parsed = 0;
  fuzz_bytes(glb, n, 6000, 0, n, 12345, &parsed);
  CHECK(parsed >= 0, "survived");
  free(glb);

  // STL, both forms.
  u8 *bin = calloc(1, 84 + 50);
  u32 one = 1;
  memcpy(bin + 80, &one, 4);
  float v[9] = {0, 0, 0, 10, 0, 0, 0, 0, 10};
  memcpy(bin + 84 + 12, v, 36);
  f = modelfile_parse(bin, 134, "b.stl", err, sizeof err);
  CHECK(f && f->is_stl && arr_len(f->prims[0].idx) == 3 && f->units == 1, "binary STL");
  modelfile_free(f);
  const char *ascii = "solid x\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1e1 0\nendloop\nendfacet\nendsolid";
  u8 *at = malloc(strlen(ascii));
  memcpy(at, ascii, strlen(ascii));
  f = modelfile_parse(at, strlen(ascii), "a.stl", err, sizeof err);
  CHECK(f && NEAR(f->box.max.y, 10, 1e-6), "text STL");
  modelfile_free(f);
  u8 *junk = calloc(1, 16);
  f = modelfile_parse(junk, 16, "j.stl", err, sizeof err);
  CHECK(!f, "junk is refused");

  // The real one: a project reads as one layer of file parts.
  AssemblySpec *s = spec_load("public/projects/motor/project.json", err, sizeof err);
  CHECK(s, "project: %s", err);
  if (s) {
    CHECK(arr_len(s->layers) == 1 && arr_len(s->layers[0].parts) > 5, "project parts");
    Model *m = model_build(s, (BuildOpts){.paint = false});
    CHECK(!m->file_error, "model file: %s", m->file_error ? m->file_error : "");
    CHECK(!m->layered && arr_len(m->pieces) == arr_len(s->layers[0].parts), "each part is a piece");
    CHECK(m->radius > 5 && m->radius < 1000, "units come out in millimetres: radius %f", (double)m->radius);
    CHECK(m->credit != NULL, "the model's credit reaches the model");
    int bell = model_find(m, "Bell");
    CHECK(bell >= 0, "named part");
    Box before = node_box_now(m, bell);
    model_set_explode(m, 1);
    model_update(m);
    Box after = node_box_now(m, bell);
    CHECK(v3dist(box_center(before), box_center(after)) > 1, "the part moves when taken apart");
    model_free(m);
    char *text = spec_write(s);
    AssemblySpec *again = spec_parse(text, err, sizeof err);
    CHECK(again && arr_len(again->layers[0].parts[0].nodes) == arr_len(s->layers[0].parts[0].nodes), "saved as a device, object names survive");
    free(text);
    spec_free(again);
    spec_free(s);
  }
  // Fuzz the real file's JSON and chunk headers; its geometry bytes can only change how it looks.
  size_t mn = 0;
  char *motor = file_read("public/projects/motor/motor.glb", &mn);
  CHECK(motor != NULL, "motor.glb");
  if (motor) {
    u32 jl;
    memcpy(&jl, motor + 12, 4);
    parsed = 0;
    fuzz_bytes((const u8 *)motor, mn, 40, 0, 20 + (size_t)jl + 8, 777, &parsed);
    CHECK(parsed >= 0, "survived");
    ModelFile *mf = modelfile_parse((u8 *)motor, mn, "motor", err, sizeof err);
    CHECK(mf && arr_len(mf->images) == 3, "images found");
    if (mf) {
      int w = 0, h = 0;
      u8 *px = modelfile_image(mf, 0, &w, &h);
      CHECK(px && w > 0 && h > 0, "image decodes");
      free(px);
      modelfile_free(mf);
    }
  }
}

int main(void) {
  test_json();
  test_logic();
  test_round_trip();
  test_golden();
  test_spread();
  test_pick();
  test_paint();
  test_editing();
  test_model_files();
  modelfile_cache_clear();
  printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
