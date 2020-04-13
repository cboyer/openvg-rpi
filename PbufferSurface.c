/* OPENVG OFFSCREEN RENDERING EXAMPLE FOR RASPBERRY PI - PbufferSurface
 * C.Boyer, 2020
 * 
 * Needed libraries: OpenVG, EGL, OpenGL ES, Freetype2, Glib2, stb
 * Compile with:
 * gcc PbufferSurface.c -O2 -Wall -Werror -L/opt/vc/lib -lbrcmEGL -lbrcmGLESv2 -lbcm_host -I/opt/vc/include -I/opt/vc/include/interface/vmcs_host/linux -I/opt/vc/include/interface/vcos/pthreads  -I/usr/include/stb -lstb -I/usr/include/freetype2 -lfreetype -I/usr/include/glib-2.0 -I/usr/lib/arm-linux-gnueabihf/glib-2.0/include -lglib-2.0
 */

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <VG/openvg.h>
#include <VG/vgu.h>
#include "bcm_host.h"

#include <glib.h>
#include <ft2build.h>
#include <freetype/ftglyph.h>
#include <freetype/ftstroke.h>
#include FT_FREETYPE_H
#include FT_STROKER_H

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define PICTURE_WIDTH 1920
#define PICTURE_HEIGHT 1080

#define FONT_MAP ":/?0123456789abcdefghijklmnopqrstuvwxyz" // Cached characters
#define FONT_MAP_SIZE sizeof(FONT_MAP)-1                    // Cached characters size
#define FONT_TTF "/home/pi/GlassGauge.ttf"                  // TTF font file
#define FONT_SIZE 100                                       // Font size
#define FONT_ESCAPMENT 50.0f                                // Font escapment, recommand 50% of FONT_SIZE
#define FONT_STROKE_SIZE 2.0f                               // Stroke size

// From /opt/vc/src/hello_pi/libs/vgfont/vgft.c
#define SEGMENTS_COUNT_MAX 256
#define COORDS_COUNT_MAX 1024

static VGuint segments_count;
static VGubyte segments[SEGMENTS_COUNT_MAX];
static VGuint coords_count;
static VGfloat coords[COORDS_COUNT_MAX];

FT_Library ft_library;
FT_Face ft_face;


typedef struct {
    int char_map[FONT_MAP_SIZE];
    VGPath cache[FONT_MAP_SIZE];
    GHashTable *index;
} VG_WRITING;

typedef struct {
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    EGLConfig config;
} EGL_STATE_T;

struct egl_manager {
    EGLNativeDisplayType xdpy;
    EGLNativeWindowType xwin;
    EGLNativePixmapType xpix;
    EGLDisplay dpy;
    EGLConfig conf;
    EGLContext ctx;
    EGLSurface win;
    EGLSurface pix;
    EGLSurface pbuf;
    EGLBoolean verbose;
    EGLint major, minor;
};


EGL_STATE_T state, *p_state = &state;
uint32_t screen_width;
uint32_t screen_height;
VG_WRITING glyphs;


// Initialize EGL
void init_egl(EGL_STATE_T *state) {
    EGLint num_configs;
    EGLBoolean result;

    static const EGLint attribute_list[] = {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENVG_BIT,
        EGL_NONE
    };

    // Get EGL display connection
    state->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

    // Initialize EGL display connection
    result = eglInitialize(state->display, NULL, NULL);

    // EGL frame buffer configuration
    result = eglChooseConfig(state->display, attribute_list, &state->config, 1, &num_configs);
    assert(EGL_FALSE != result);

    result = eglBindAPI(EGL_OPENVG_API);
    assert(EGL_FALSE != result);

    // Create EGL rendering context
    state->context = eglCreateContext(state->display, state->config, EGL_NO_CONTEXT, NULL);
    assert(state->context!=EGL_NO_CONTEXT);
}

// Initialize Dispmanx
void init_dispmanx(EGL_DISPMANX_WINDOW_T *nativewindow) {
    int32_t success = 0;

    DISPMANX_ELEMENT_HANDLE_T dispman_element;
    DISPMANX_DISPLAY_HANDLE_T dispman_display;
    DISPMANX_UPDATE_HANDLE_T dispman_update;
    VC_RECT_T dst_rect;
    VC_RECT_T src_rect;
    bcm_host_init();

    // Create EGL window surface
    success = graphics_get_display_size(0 /* lcd */, &screen_width, &screen_height);
    assert( success >= 0 );

    // Hardcoded height/width
    screen_width = PICTURE_WIDTH;
    screen_height = PICTURE_HEIGHT;

    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.width = screen_width;
    dst_rect.height = screen_height;
    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.width = screen_width << 16;
    src_rect.height = screen_height << 16;
    dispman_display = vc_dispmanx_display_open( 0 /* lcd */);
    dispman_update = vc_dispmanx_update_start( 0 );
    dispman_element = vc_dispmanx_element_add(dispman_update, dispman_display, 0/*layer*/, &dst_rect, 0/*src*/, &src_rect, DISPMANX_PROTECTION_NONE, 0 /*alpha*/, 0/*clamp*/, 0/*transform*/);

    // Build EGL_DISPMANX_WINDOW_T from Dispmanx window
    nativewindow->element = dispman_element;
    nativewindow->width = screen_width;
    nativewindow->height = screen_height;
    vc_dispmanx_update_submit_sync(dispman_update);
    printf("Dispmanx window initialized, width: %" PRIu32 ", height: %" PRIu32 ".\n", screen_width, screen_height);
}

void egl_from_dispmanx(EGL_STATE_T *state,
    EGL_DISPMANX_WINDOW_T *nativewindow) {
    EGLBoolean result;
    state->surface = eglCreateWindowSurface(state->display, state->config, nativewindow, NULL );
    assert(state->surface != EGL_NO_SURFACE);

    // Connect context to surface
    result = eglMakeCurrent(state->display, state->surface, state->surface, state->context);
    assert(EGL_FALSE != result);
}

// Sets fill color
void setfill(float color[4]) {
    VGPaint fillPaint = vgCreatePaint();
    vgSetParameteri(fillPaint, VG_PAINT_TYPE, VG_PAINT_TYPE_COLOR);
    vgSetParameterfv(fillPaint, VG_PAINT_COLOR, 4, color);
    vgSetPaint(fillPaint, VG_FILL_PATH);
    vgDestroyPaint(fillPaint);
}

// Sets the stroke color and width
void setstroke(float color[4], float width) {
    VGPaint strokePaint = vgCreatePaint();
    vgSetParameteri(strokePaint, VG_PAINT_TYPE, VG_PAINT_TYPE_COLOR);
    vgSetParameterfv(strokePaint, VG_PAINT_COLOR, 4, color);
    vgSetPaint(strokePaint, VG_STROKE_PATH);
    vgSetf(VG_STROKE_LINE_WIDTH, width);
    vgSeti(VG_STROKE_CAP_STYLE, VG_CAP_BUTT);
    vgSeti(VG_STROKE_JOIN_STYLE, VG_JOIN_MITER);
    vgDestroyPaint(strokePaint);
}

// Initialize Freetype2
void ft_init() {
    int err;
    err = FT_Init_FreeType(&ft_library);
    assert(!err);
    err = FT_New_Face(ft_library, FONT_TTF, 0, &ft_face );
    assert(!err);
    int font_size = FONT_SIZE;
    err = FT_Set_Pixel_Sizes(ft_face, 0, font_size);
    assert(!err);
}


static VGfloat float_from_26_6(FT_Pos x) {
   return (VGfloat)x / 64.0f;
}

// From /opt/vc/src/hello_pi/libs/vgfont/vgft.c
static void convert_contour(const FT_Vector *points, const char *tags, short points_count) {
   int first_coords = coords_count;

   int first = 1;
   char last_tag = 0;
   int c = 0;

   for (; points_count != 0; points++, tags++, points_count--) {
      c++;

      char tag = *tags;
      if (first) {
         assert(tag & 0x1);
         assert(c == 1); c = 0;
         segments[segments_count++] = VG_MOVE_TO;
         first = 0;
      } else if (tag & 0x1) {
         /* on curve */

         if (last_tag & 0x1) {
            /* last point was also on -- line */
            assert(c == 1); c = 0;
            segments[segments_count++] = VG_LINE_TO;
         } else {
            /* last point was off -- quad or cubic */
            if (last_tag & 0x2) {
               /* cubic */
               assert(c==3); c=0;
               segments[segments_count++] = VG_CUBIC_TO;
            } else {
               /* quad */
               assert(c==2); c=0;
               segments[segments_count++] = VG_QUAD_TO;
            }
         }
      } else {
         /* off curve */

         if (tag & 0x2) {
            /* cubic */

            assert((last_tag & 0x1) || (last_tag & 0x2)); /* last either on or off and cubic */
         } else {
            /* quad */

            if (!(last_tag & 0x1)) {
               /* last was also off curve */

               assert(!(last_tag & 0x2)); /* must be quad */

               /* add on point half-way between */
               assert(c==2); c=1;
               segments[segments_count++] = VG_QUAD_TO;
               VGfloat x = (coords[coords_count - 2] + float_from_26_6(points->x)) * 0.5f;
               VGfloat y = (coords[coords_count - 1] + float_from_26_6(points->y)) * 0.5f;
               coords[coords_count++] = x;
               coords[coords_count++] = y;
            }
         }
      }
      last_tag = tag;

      coords[coords_count++] = float_from_26_6(points->x);
      coords[coords_count++] = float_from_26_6(points->y);
   }

   if (last_tag & 0x1) {
      /* last point was also on -- line (implicit with close path) */
      assert(c==0);
   } else {
      ++c;

      /* last point was off -- quad or cubic */
      if (last_tag & 0x2) {
         /* cubic */
         assert(c==3); c=0;
         segments[segments_count++] = VG_CUBIC_TO;
      } else {
         /* quad */
         assert(c==2); c=0;
         segments[segments_count++] = VG_QUAD_TO;
      }
      coords[coords_count++] = coords[first_coords + 0];
      coords[coords_count++] = coords[first_coords + 1];
   }

   segments[segments_count++] = VG_CLOSE_PATH;
}

// From /opt/vc/src/hello_pi/libs/vgfont/vgft.c
static void convert_outline(const FT_Vector *points, const char *tags, const short *contours, short contours_count, short points_count) {
   segments_count = 0;
   coords_count = 0;

   short last_contour = 0;
   for (; contours_count != 0; contours++, contours_count--) {
      short contour = *contours + 1;
      convert_contour(points + last_contour, tags + last_contour, contour - last_contour);
      last_contour = contour;
   }
   assert(last_contour == points_count);

   assert(segments_count <= SEGMENTS_COUNT_MAX); /* oops... we overwrote some memory */
   assert(coords_count <= COORDS_COUNT_MAX);
}

// Create VGPath from character
VGPath create_vgpath_from_char(int *ch) {
    printf("Create path for character '%c'.\n", *ch);

    int glyph_index = FT_Get_Char_Index(ft_face, *ch);
    if (glyph_index == 0) 
        printf("No glyph found in TTF for character '%c'.\n", *ch);

    FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_NO_HINTING| FT_LOAD_LINEAR_DESIGN); // | FT_LOAD_NO_SCALE);

    VGPath vg_path;

    FT_Outline *outline = &ft_face->glyph->outline;
    if (outline->n_contours != 0) {
        vg_path = vgCreatePath(VG_PATH_FORMAT_STANDARD, VG_PATH_DATATYPE_F, 1.0f, 0.0f, 0, 0, VG_PATH_CAPABILITY_ALL);
        assert(vg_path != VG_INVALID_HANDLE);

        convert_outline(outline->points, outline->tags, outline->contours, outline->n_contours, outline->n_points);
        vgAppendPathData(vg_path, segments_count, segments, coords);
        vgRemovePathCapabilities(vg_path, VG_PATH_CAPABILITY_APPEND_FROM |
                                          VG_PATH_CAPABILITY_APPEND_TO |
                                          VG_PATH_CAPABILITY_MODIFY | 
                                          VG_PATH_CAPABILITY_TRANSFORM_FROM |
                                          VG_PATH_CAPABILITY_TRANSFORM_TO | 
                                          VG_PATH_CAPABILITY_INTERPOLATE_FROM |
                                          VG_PATH_CAPABILITY_INTERPOLATE_TO);

    }
    else {
        vg_path = VG_INVALID_HANDLE;
    }

    //printf("Width %ld, height %ld advance %ld\n", ft_face->glyph->metrics.width, ft_face->glyph->metrics.height, ft_face->glyph->linearHoriAdvance);
    return vg_path;
}


// Initialize characters cache
void init_glyph_cache() {
    printf("Initializing glyph cache...\n");
    glyphs.index = g_hash_table_new(g_int_hash, g_int_equal);

    for(int i = 0; i < FONT_MAP_SIZE; i++) {
        glyphs.char_map[i] = FONT_MAP[i];

        glyphs.cache[i] = create_vgpath_from_char(&glyphs.char_map[i]);

        if(!g_hash_table_insert(glyphs.index, &glyphs.char_map[i], &glyphs.cache[i]))
            printf("Insert failed for '%c'\n", glyphs.char_map[i]);
    }
}

// Destroy characters cache
void destroy_glyph_cache() {
    for(int i = 0; i < FONT_MAP_SIZE; i++) {
        vgDestroyPath(glyphs.cache[i]);
    }
    glyphs.char_map[0] = '\0';
    g_hash_table_destroy(glyphs.index);
}

// Get VGPath for corresponding character, indexed with GHashTable
VGPath get_glyph_from_cache(int character) {
    VGPath *data_pointer = g_hash_table_lookup(glyphs.index, &character);

    if(data_pointer == NULL) {
        printf("Can't find character '%c' in cache.\n", character);
        return VG_INVALID_HANDLE;
    }
    else
        return *data_pointer;
}

// Draw each VGPath for each character in string
void vgDrawString(char string[], VGfloat pos_x, VGfloat pos_y) {
    VGPath path = vgCreatePath(VG_PATH_FORMAT_STANDARD, VG_PATH_DATATYPE_F, 1.0f, 0.0f, 0.0f, 0.0f, VG_PATH_CAPABILITY_ALL);

    for(int i = 0; i < strlen(string); i++) {
        if(string[i] != ' ') {
            path = get_glyph_from_cache(string[i]);
            vgTranslate(pos_x + i * FONT_ESCAPMENT, pos_y);
            vgDrawPath(path, VG_STROKE_PATH | VG_FILL_PATH);
            vgTranslate(-(pos_x + i * FONT_ESCAPMENT), -pos_y);
        }
    }
}

// Main drawing function
void text_offscreen_surface() {

    ft_init();

    EGLSurface pbuffer_surface;
    EGLContext context;

    static const EGLint attribute_list[] = {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_BUFFER_SIZE, 24,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENVG_BIT,
        EGL_NONE
    };

    EGLint pbuffer_attrib[] = {
        EGL_WIDTH, screen_width,
        EGL_HEIGHT, screen_height,
        EGL_NONE
     };

    EGLConfig config;

    int result, num_configs;
    result = eglChooseConfig(p_state->display, attribute_list, &config, 1, &num_configs);

    result = eglBindAPI(EGL_OPENVG_API);
    assert(EGL_FALSE != result);

    context = eglCreateContext(p_state->display, config, EGL_NO_CONTEXT, NULL);

    // Use of PbufferSurface for offscreen rendering
    pbuffer_surface = eglCreatePbufferSurface(p_state->display, config, pbuffer_attrib);

    if (pbuffer_surface == EGL_NO_SURFACE) {
        fprintf(stderr, "Couldn't create pbuffer surface.\n");
        fprintf(stderr, "Error code %x\n", eglGetError());
        exit(1);
    }
 
    eglMakeCurrent(p_state->display, pbuffer_surface, pbuffer_surface, context);
    init_glyph_cache();

    //VGfloat color_purple[4] = {0.4, 0.1, 1.0, 1.0};
    //VGfloat color_yellow[4] = {1.0, 1.0, 0.0, 1.0};
    VGfloat color_transparent[4] = {0.0, 0.0, 0.0, 0.0};
    VGfloat color_white[4] = {1.0, 1.0, 1.0, 1.0};
    VGfloat color_blue[4] = {0.0, 0.0, 1.0, 1.0};
    VGfloat color_red[4] = {1.0, 0.0, 0.0, 1.0};

    setfill(color_transparent);
    setstroke(color_white, FONT_STROKE_SIZE);

    // Matrix transformation to reorganize pixels according to stbi_write_png reading order
    vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
    VGfloat matrix_pixel_order_stbi[] = { 1.0, 0, 0.0, 0, -1.0, 0.0, 0, screen_height, 1.0 }; // Rotation -180, translate y -1080px
    vgMultMatrix(matrix_pixel_order_stbi);

    eglMakeCurrent(p_state->display, pbuffer_surface, pbuffer_surface, p_state->context);

    void *ScreenBuffer = malloc(screen_width * screen_height * 4); // 4 = channel number (RGBA)
    char filename[100] = "";

    // Drawing pictures here
    for(int i = 0; i < 3; i++) {
        vgClear(0, 0, screen_width, screen_height);
        eglMakeCurrent(p_state->display, pbuffer_surface, pbuffer_surface, context);
        VGPath path = vgCreatePath(VG_PATH_FORMAT_STANDARD, VG_PATH_DATATYPE_F, 1.0f, 0.0f, 0.0f, 0.0f, VG_PATH_CAPABILITY_ALL);

        vguRoundRect(path, 28, 10, 200, 60, 15.0, 15.0);
        vguEllipse(path, 128 + 100 * i, 200, 60, 40);

        if(i % 2)
            setfill(color_blue);
        else
            setfill(color_red);

        vgDrawPath(path, VG_FILL_PATH);

        //vgClearPath(path, VG_PATH_CAPABILITY_ALL);
        vgDestroyPath(path);

        vgDrawString("string test 1 x:500 y:500", 500.0f, 500.0f);
        vgDrawString("string test 2 x:300 y:300", 300.0f, 300.0f);

        //vgFlush();
        vgFinish();

        eglMakeCurrent(p_state->display, pbuffer_surface, pbuffer_surface, p_state->context);
        assert(vgGetError() == VG_NO_ERROR);

        // Read memory and save to PNG file to see something (bad performances)
        vgReadPixels(ScreenBuffer, (screen_width * 4), VG_sABGR_8888, 0, 0, screen_width, screen_height);
        sprintf(filename, "img%d.png", i);
        stbi_write_png(filename, screen_width, screen_height, 4, ScreenBuffer, (screen_width * 4) ); //channel= 1=Y, 2=YA, 3=RGB, 4=RGBA
        printf("%s saved\n", filename);
    }

    // Cleanup
    assert(eglGetError() == EGL_SUCCESS);
    free(ScreenBuffer);
    destroy_glyph_cache();
    eglMakeCurrent(p_state->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);   
    eglDestroyContext(p_state->display, context);   
    context = NULL;
    eglDestroySurface(p_state->display, pbuffer_surface);   
    pbuffer_surface = NULL;  
}



int main(int argc, char *argv[]) {

    EGL_DISPMANX_WINDOW_T nativewindow;

    init_egl(p_state);
    init_dispmanx(&nativewindow);
    egl_from_dispmanx(p_state, &nativewindow);

    text_offscreen_surface();

    eglTerminate(p_state->display);
    exit(0);
}
