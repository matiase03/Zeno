/* Capa minima OpenGL ES 1.1 (funciones fijas) sobre OpenGL ES 2.0.
 * Solo implementa las ~23 funciones que importa libgameDSO.so. */
#include "common.h"
#include <SDL2/SDL.h>

typedef unsigned int GLuint; typedef int GLint; typedef unsigned int GLenum; typedef int GLsizei;
typedef float GLfloat; typedef unsigned char GLboolean; typedef char GLchar; typedef intptr_t GLsizeiptr;

#define GL_FLOAT 0x1406
#define GL_BYTE 0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_FIXED 0x140C
#define GL_ARRAY_BUFFER 0x8892
#define GL_STREAM_DRAW 0x88E0
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_BLEND 0x0BE2
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_CLAMP_TO_EDGE 0x812F

static struct {
    GLuint (*CreateShader)(GLenum); void (*ShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *);
    void (*CompileShader)(GLuint); GLuint (*CreateProgram)(void); void (*AttachShader)(GLuint, GLuint);
    void (*BindAttribLocation)(GLuint, GLuint, const GLchar *); void (*LinkProgram)(GLuint); void (*UseProgram)(GLuint);
    GLint (*GetUniformLocation)(GLuint, const GLchar *); void (*UniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat *);
    void (*Uniform1i)(GLint, GLint); void (*VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
    void (*EnableVertexAttribArray)(GLuint); void (*DisableVertexAttribArray)(GLuint);
    void (*VertexAttrib4f)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
    void (*GenBuffers)(GLsizei, GLuint *); void (*BindBuffer)(GLenum, GLuint); void (*BufferData)(GLenum, GLsizeiptr, const void *, GLenum);
    void (*DrawArrays)(GLenum, GLint, GLsizei); void (*BlendFunc)(GLenum, GLenum);
    void (*Enable)(GLenum); void (*Disable)(GLenum); void (*Viewport)(GLint, GLint, GLsizei, GLsizei);
    void (*Clear)(GLuint); void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (*BindTexture)(GLenum, GLuint); void (*GenTextures)(GLsizei, GLuint *);
    void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
    void (*TexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *);
    void (*TexParameteri)(GLenum, GLenum, GLint); void (*ActiveTexture)(GLenum);
    void (*GetShaderiv)(GLuint, GLenum, GLint *); void (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
    void (*GetProgramiv)(GLuint, GLenum, GLint *); GLenum (*GetError)(void);
} G;

#define LOADGL(n) do { *(void **)&G.n = SDL_GL_GetProcAddress("gl" #n); if (!G.n) LOG("[GLES] FALTA gl" #n); } while (0)

static GLuint prog, vbo;
static GLint u_mvp, u_tex, u_mode, u_use;
static float proj[16], mv[16]; static float *cur = NULL; static int matmode = 0x1700;
static int tex2d_on, tc_on, col_on, vtx_on; static int texenv_mode;   /* 0 modulate, 1 replace */

typedef struct { int size, type, stride; const void *ptr; } Arr;
static Arr a_vtx, a_col, a_tc;

static void ident(float *m) { memset(m, 0, 64); m[0] = m[5] = m[10] = m[15] = 1; }
static void mul(float *o, const float *a, const float *b) { /* column-major o = a*b */
    float t[16];
    for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) {
        float s = 0; for (int k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k]; t[c * 4 + r] = s; }
    memcpy(o, t, 64);
}

static GLuint compile(GLenum type, const char *src) {
    GLuint s = G.CreateShader(type); G.ShaderSource(s, 1, &src, NULL); G.CompileShader(s);
    GLint ok = 0; G.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; G.GetShaderInfoLog(s, 512, NULL, log); LOG("[GLES] shader error: %s", log); }
    return s;
}

int gles_init(void) {
    LOADGL(CreateShader); LOADGL(ShaderSource); LOADGL(CompileShader); LOADGL(CreateProgram); LOADGL(AttachShader);
    LOADGL(BindAttribLocation); LOADGL(LinkProgram); LOADGL(UseProgram); LOADGL(GetUniformLocation); LOADGL(UniformMatrix4fv);
    LOADGL(Uniform1i); LOADGL(VertexAttribPointer); LOADGL(EnableVertexAttribArray); LOADGL(DisableVertexAttribArray);
    LOADGL(VertexAttrib4f); LOADGL(GenBuffers); LOADGL(BindBuffer); LOADGL(BufferData); LOADGL(DrawArrays); LOADGL(BlendFunc);
    LOADGL(Enable); LOADGL(Disable); LOADGL(Viewport); LOADGL(Clear); LOADGL(ClearColor); LOADGL(BindTexture); LOADGL(GenTextures);
    LOADGL(TexImage2D); LOADGL(TexSubImage2D); LOADGL(TexParameteri); LOADGL(ActiveTexture); LOADGL(GetShaderiv);
    LOADGL(GetShaderInfoLog); LOADGL(GetProgramiv); LOADGL(GetError);
    if (!G.CreateShader || !G.DrawArrays) return -1;
    const char *vs =
        "attribute vec4 aPos; attribute vec2 aTex; attribute vec4 aCol; uniform mat4 uMVP;\n"
        "varying vec2 vTex; varying vec4 vCol;\n"
        "void main(){ gl_Position = uMVP * aPos; vTex = aTex; vCol = aCol; }\n";
    const char *fs =
        "precision mediump float; varying vec2 vTex; varying vec4 vCol; uniform sampler2D uTex; uniform int uUse; uniform int uMode;\n"
        "void main(){ if (uUse == 0) { gl_FragColor = vCol; } else { vec4 t = texture2D(uTex, vTex);\n"
        "  if (uMode == 1) gl_FragColor = t; else gl_FragColor = t * vCol; } }\n";
    GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
    prog = G.CreateProgram(); G.AttachShader(prog, v); G.AttachShader(prog, f);
    G.BindAttribLocation(prog, 0, "aPos"); G.BindAttribLocation(prog, 1, "aTex"); G.BindAttribLocation(prog, 2, "aCol");
    G.LinkProgram(prog);
    GLint ok = 0; G.GetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { LOG("[GLES] link error"); return -1; }
    G.UseProgram(prog);
    u_mvp = G.GetUniformLocation(prog, "uMVP"); u_tex = G.GetUniformLocation(prog, "uTex");
    u_use = G.GetUniformLocation(prog, "uUse"); u_mode = G.GetUniformLocation(prog, "uMode");
    G.Uniform1i(u_tex, 0);
    G.GenBuffers(1, &vbo);
    ident(proj); ident(mv); cur = mv;
    G.Enable(GL_BLEND); G.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    LOG("[GLES] capa GLES1->GLES2 lista (%s)", (const char *)SDL_GL_GetProcAddress("glGetString") ? "ok" : "?");
    return 0;
}

/* ---------- las funciones GLES1 que importa el juego (se enganchan por GOT) ---------- */
SOFT void gles1_glActiveTexture(GLenum t) { G.ActiveTexture(t); }
SOFT void gles1_glBindTexture(GLenum t, GLuint n) { G.BindTexture(t, n); }
SOFT void gles1_glClear(GLuint m) { G.Clear(m); }
SOFT void gles1_glClearColorx(int r, int g, int b, int a) { G.ClearColor(r / 65536.0f, g / 65536.0f, b / 65536.0f, a / 65536.0f); }
SOFT void gles1_glColorPointer(GLint s, GLenum t, GLsizei st, const void *p) { a_col = (Arr){ s, (int)t, st, p }; }
SOFT void gles1_glNormalPointer(GLenum t, GLsizei st, const void *p) { }
SOFT void gles1_glTexCoordPointer(GLint s, GLenum t, GLsizei st, const void *p) { a_tc = (Arr){ s, (int)t, st, p }; }
SOFT void gles1_glVertexPointer(GLint s, GLenum t, GLsizei st, const void *p) { a_vtx = (Arr){ s, (int)t, st, p }; }
SOFT void gles1_glDisable(GLenum c) {
    switch (c) {
    case GL_TEXTURE_2D: tex2d_on = 0; return;
    case 0x0BE2: case 0x0B44: case 0x0B71: case 0x0BD0: case 0x0C11: case 0x0B90: case 0x8037: G.Disable(c); return;
    default: return;
    }
}
SOFT void gles1_glEnable(GLenum c) {
    switch (c) {
    case GL_TEXTURE_2D: tex2d_on = 1; return;
    case 0x0BE2: case 0x0B44: case 0x0B71: case 0x0BD0: case 0x0C11: case 0x0B90: case 0x8037: G.Enable(c); return;
    default: log_once("en", "[GLES] glEnable(0x%x) ignorado", c); return;
    }
}
SOFT void gles1_glEnableClientState(GLenum a) { if (a == 0x8074) vtx_on = 1; else if (a == 0x8076) col_on = 1; else if (a == 0x8078) tc_on = 1; }
SOFT void gles1_glDisableClientState(GLenum a) { if (a == 0x8074) vtx_on = 0; else if (a == 0x8076) col_on = 0; else if (a == 0x8078) tc_on = 0; }
SOFT void gles1_glGenTextures(GLsizei n, GLuint *t) { G.GenTextures(n, t); }
SOFT void gles1_glHint(GLenum a, GLenum b) { }
SOFT void gles1_glLoadIdentity(void) { if (cur) ident(cur); }
SOFT void gles1_glMatrixMode(GLenum m) { matmode = (int)m; cur = (m == 0x1701) ? proj : (m == 0x1700 ? mv : NULL); }
SOFT void gles1_glOrthof(float l, float r, float b, float t, float n, float f) {
    if (!cur) return;
    float o[16]; ident(o);
    o[0] = 2 / (r - l); o[5] = 2 / (t - b); o[10] = -2 / (f - n);
    o[12] = -(r + l) / (r - l); o[13] = -(t + b) / (t - b); o[14] = -(f + n) / (f - n);
    mul(cur, cur, o);
}
SOFT void gles1_glTexEnvf(GLenum target, GLenum pname, float param) {
    if (pname == 0x2200) texenv_mode = ((int)param == 0x1E01) ? 1 : 0;
}
SOFT void gles1_glTexParameterx(GLenum t, GLenum p, int v) {
    if (p == 0x8191) return;                       /* GL_GENERATE_MIPMAP no existe en ES2 */
    if (v == 0x2900) v = GL_CLAMP_TO_EDGE;         /* GL_CLAMP */
    if (p == 0x2801) { if (v == 0x2700 || v == 0x2702) v = 0x2600; else if (v == 0x2701 || v == 0x2703) v = 0x2601; }
    G.TexParameteri(t, p, v);
}
SOFT void gles1_glTexImage2D(GLenum t, GLint lv, GLint ifmt, GLsizei w, GLsizei h, GLint b, GLenum fmt, GLenum ty, const void *px) {
    G.TexImage2D(t, lv, ifmt, w, h, b, fmt, ty, px);
}
SOFT void gles1_glTexSubImage2D(GLenum t, GLint lv, GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum ty, const void *px) {
    G.TexSubImage2D(t, lv, x, y, w, h, fmt, ty, px);
}
SOFT void gles1_glViewport(GLint x, GLint y, GLsizei w, GLsizei h) { G.Viewport(x, y, w, h); }

static void fetch(const Arr *a, int idx, int ncomp, int norm, float *out) {
    const unsigned char *p = (const unsigned char *)a->ptr;
    int esz = (a->type == GL_FLOAT || a->type == GL_FIXED) ? 4 : (a->type == GL_SHORT || a->type == GL_UNSIGNED_SHORT) ? 2 : 1;
    int stride = a->stride ? a->stride : esz * a->size;
    p += (size_t)idx * stride;
    for (int i = 0; i < ncomp && i < a->size; i++) {
        float v = 0;
        switch (a->type) {
        case GL_FLOAT: v = ((const float *)p)[i]; break;
        case GL_FIXED: v = ((const int *)p)[i] / 65536.0f; break;
        case GL_SHORT: v = ((const short *)p)[i]; break;
        case GL_UNSIGNED_SHORT: v = ((const unsigned short *)p)[i]; break;
        case GL_BYTE: v = ((const signed char *)p)[i]; if (norm) v /= 127.0f; break;
        case GL_UNSIGNED_BYTE: v = p[i]; if (norm) v /= 255.0f; break;
        }
        out[i] = v;
    }
}

static float *scratch; static size_t scratch_cap;
SOFT void gles1_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    if (!vtx_on || !a_vtx.ptr || count <= 0) return;
    size_t need = (size_t)count * 10 * sizeof(float);
    if (need > scratch_cap) { scratch = realloc(scratch, need); scratch_cap = need; }
    for (int i = 0; i < count; i++) {
        float *v = scratch + i * 10;
        v[0] = v[1] = v[2] = 0; v[3] = 1; v[4] = v[5] = 0; v[6] = v[7] = v[8] = v[9] = 1;
        fetch(&a_vtx, first + i, 4, 0, v);
        if (tc_on && a_tc.ptr) fetch(&a_tc, first + i, 2, 0, v + 4);
        if (col_on && a_col.ptr) fetch(&a_col, first + i, 4, 1, v + 6);
    }
    float m[16]; mul(m, proj, mv);
    G.UniformMatrix4fv(u_mvp, 1, 0, m);
    G.Uniform1i(u_use, tex2d_on); G.Uniform1i(u_mode, texenv_mode);
    G.BindBuffer(GL_ARRAY_BUFFER, vbo);
    G.BufferData(GL_ARRAY_BUFFER, need, scratch, GL_STREAM_DRAW);
    G.EnableVertexAttribArray(0); G.VertexAttribPointer(0, 4, GL_FLOAT, 0, 40, (void *)0);
    G.EnableVertexAttribArray(1); G.VertexAttribPointer(1, 2, GL_FLOAT, 0, 40, (void *)16);
    G.EnableVertexAttribArray(2); G.VertexAttribPointer(2, 4, GL_FLOAT, 0, 40, (void *)24);
    G.DrawArrays(mode, 0, count);
}
