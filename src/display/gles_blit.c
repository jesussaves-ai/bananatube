/* gles_blit.c */
#include "display/gles_blit.h"
#include "util.h"

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <string.h>

static GLuint g_prog;
static GLint  g_pos, g_uv, g_sampler, g_viewport;

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = "";
        glGetShaderInfoLog(s, sizeof log - 1, NULL, log);
        log_line("blit: shader: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool blit_init(void)
{
    static const char *vs =
        "attribute vec2 a_pos;\n"
        "attribute vec2 a_uv;\n"
        "varying vec2 v_uv;\n"
        "uniform vec4 u_rect;\n"       /* x, y, w, h in normalised device units */
        "uniform vec4 u_view;\n"       /* dst_w, dst_h, 0, 0 */
        "void main() {\n"
        "  vec2 p = u_rect.xy + a_pos * u_rect.zw;\n"
        "  gl_Position = vec4(p.x / u_view.x * 2.0 - 1.0, 1.0 - p.y / u_view.y * 2.0, 0.0, 1.0);\n"
        "  v_uv = a_uv;\n"
        "}\n";
    static const char *fs =
        "precision mediump float;\n"
        "varying vec2 v_uv;\n"
        "uniform sampler2D u_tex;\n"
        "void main() { gl_FragColor = texture2D(u_tex, v_uv); }\n";

    GLuint v = compile(GL_VERTEX_SHADER, vs);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return false;

    g_prog = glCreateProgram();
    glAttachShader(g_prog, v);
    glAttachShader(g_prog, f);
    glLinkProgram(g_prog);
    GLint ok = 0;
    glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512] = "";
        glGetProgramInfoLog(g_prog, sizeof log - 1, NULL, log);
        log_line("blit: link: %s", log);
        return false;
    }
    glDeleteShader(v);
    glDeleteShader(f);

    g_pos = glGetAttribLocation(g_prog, "a_pos");
    g_uv = glGetAttribLocation(g_prog, "a_uv");
    g_sampler = glGetUniformLocation(g_prog, "u_tex");
    g_viewport = glGetUniformLocation(g_prog, "u_view");
    return true;
}

void blit_fini(void)
{
    if (g_prog) glDeleteProgram(g_prog);
    g_prog = 0;
}

unsigned blit_upload(unsigned tex, const uint32_t *argb, int w, int h)
{
    if (!argb || w <= 0 || h <= 0) return tex;
    if (!tex) {
        GLuint t = 0;
        glGenTextures(1, &t);
        tex = t;
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    /* ARGB32 in memory (what the renderer writes) is GL_RGBA byte order with alpha first: BGRA. */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, argb);
    return tex;
}

void blit_free(unsigned tex)
{
    if (tex) { GLuint t = tex; glDeleteTextures(1, &t); }
}

void blit_draw(unsigned tex, int tex_w, int tex_h, int x, int y, int w, int h, int dst_w, int dst_h)
{
    if (!g_prog || !tex || w <= 0 || h <= 0) return;
    (void)tex_w; (void)tex_h;

    static const GLfloat pos[8] = { 0, 0, 1, 0, 0, 1, 1, 1 };
    static const GLfloat uv[8]  = { 0, 0, 1, 0, 0, 1, 1, 1 };

    glUseProgram(g_prog);
    glUniform4f(glGetUniformLocation(g_prog, "u_rect"), (float)x, (float)y, (float)w, (float)h);
    glUniform4f(g_viewport, (float)dst_w, (float)dst_h, 0, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(g_sampler, 0);

    glVertexAttribPointer((GLuint)g_pos, 2, GL_FLOAT, GL_FALSE, 0, pos);
    glVertexAttribPointer((GLuint)g_uv, 2, GL_FLOAT, GL_FALSE, 0, uv);
    glEnableVertexAttribArray((GLuint)g_pos);
    glEnableVertexAttribArray((GLuint)g_uv);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray((GLuint)g_pos);
    glDisableVertexAttribArray((GLuint)g_uv);
    glDisable(GL_BLEND);
}
