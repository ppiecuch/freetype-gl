/* Freetype GL - A C OpenGL Freetype engine
 *
 * Distributed under the OSI-approved BSD 2-Clause License.  See accompanying
 * file `LICENSE` for more details.
 */
#include "opengl.h"
#include "vec234.h"
#include "vector.h"
#include "freetype-gl.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <limits.h>


#ifndef WIN32
#   define PRIzu "zu"
#else
#   define PRIzu "Iu"
#endif

// The type of PNG image. It determines how the pixels are stored.
typedef enum {
    PNG_GRAYSCALE = 0,          /**< 256 shades of gray, 8bit per pixel  */
    PNG_RGB = 2,                /**< 24bit RGB values */
    PNG_PALETTE = 3,            /**< Up to 256 RGBA palette colors, 8bit per pixel */
    PNG_GRAYSCALE_ALPHA = 4,    /**< 256 shades of gray plus alpha channel, 16bit per pixel */
    PNG_RGBA = 6                /**< 24bit RGB values plus 8bit alpha channel */
} libattopng_type_t;

typedef struct libattopng_t libattopng_t;

// quick png library:
libattopng_t *libattopng_new(size_t width, size_t height, libattopng_type_t type);
void libattopng_start_stream(libattopng_t* png, size_t x, size_t y);
void libattopng_put_pixel(libattopng_t* png, uint32_t color);
void libattopng_destroy(libattopng_t *png);
int libattopng_save(libattopng_t *png, const char *filename);
// end.

#ifdef __APPLE__
char *strrstr(const char *haystack, const char *needle);
#endif

// ------------------------------------------------------------- print help ---
void print_help()
{
    fprintf( stderr, "Usage: makefont [--help] --font <font file> "
             "--header <header file> --size <font size> "
             "--variable <variable name> --texture <texture size> "
             "--padding <left,right,top,bottom> --spacing <spacing value> "
             "--rendermode <one of 'normal', 'outline_edge', 'outline_positive', 'outline_negative' or 'sdf'>\n" );
}

// ------------------------------------------------------------- dump image ---
void dumpimage_tga( const unsigned char *buffer, const int width, const int height, const int depth, const char* path )
{
    FILE* out = fopen( path, "wb" );

    uint8_t *data = (uint8_t*)buffer;
    uint8_t bpp = depth*8;
    if(depth == 1) { // upscale from 8 to 16bit
        data = malloc(width * height * 2); for(int i=0; i<width*height*depth; i+=1) {
            uint16_t tmp;
            #define BIT(index) (1 << (index))
            #define FIVE_BITS (BIT(0)|BIT(1)|BIT(2)|BIT(3)|BIT(4))
            tmp  =  (buffer[i] >> 3) & FIVE_BITS; // r
            tmp |= ((buffer[i] >> 3) & FIVE_BITS) << 5; // g
            tmp |= ((buffer[i] >> 3) & FIVE_BITS) << 10; //b
            if (buffer[i] > 127) tmp |= BIT(15); // a
            #undef FIVE_BITS
            #undef BIT
            data[i*2+0] = (uint8_t) (tmp & 0x00FF);
            data[i*2+1] = (uint8_t)((tmp & 0xFF00) >> 8);
        }
        bpp = 16;
    }

    const int chan = bpp / 8;

    uint8_t tga_header[18] = { 0 };
    // Data code type -- 2 - uncompressed RGB image.
    tga_header[2] = 2;
    // Image width - low byte
    tga_header[12] = width & 0xFF;
    // Image width - high byte
    tga_header[13] = (width >> 8) & 0xFF;
    // Image height - low byte
    tga_header[14] = height & 0xFF;
    // Image height - high byte
    tga_header[15] = (height >> 8) & 0xFF;
    // Color bit depth
    tga_header[16] = bpp; // 16,24,32
    if(chan == 4)
        tga_header[17] = 4; // bottom left image (0x00) + 8 bit alpha (0x4)
    fwrite( tga_header, sizeof(uint8_t), 18, out );

    // save flipped data:
    const size_t lsize = width * chan;
    const uint8_t *data_end = data + width*height*chan - lsize;
    for(int y=0; y<height; y++)
    {
        fwrite( data_end - y*lsize, sizeof(uint8_t), lsize, out );
    }

    fclose( out );
    if(data != (uint8_t*)buffer)
        free( data );
}

void dumpimage_png( const unsigned char *buffer, const int width, const int height, const int depth, const char* path )
{
    libattopng_t* png = libattopng_new(width, height, PNG_RGBA);

    libattopng_start_stream(png, 0, 0);

    uint8_t *data = (uint8_t *)buffer;
    for(int y=0; y<height; y++)
    {
        for (int x = 0; x < width; x++) {
            uint32_t pixel = 0;
            switch (depth) {
                case 1: pixel = 0x00ffffff | (*data << 24); break;
                case 2: pixel = 0x00ffffff | (*data+1) << 24; break;
                case 3: pixel = 0xff000000 | (*data+2)<<16  | (*data+1)<<8 | *data; break;
                case 4: pixel = *(uint32_t*)data; break;
            }
            libattopng_put_pixel(png, pixel);
            data += depth;
        }
    }

    libattopng_save(png, path);
    libattopng_destroy(png);
}

void dumpimage( const unsigned char *buffer, const int width, const int height, const int depth, const char* path )
{
    const char *ext = strrstr(path, ".png");
    if (ext == 0 || ext == path)
        dumpimage_png(buffer, width, height, depth, path);
    else
        dumpimage_tga(buffer, width, height, depth, path);
}

const char *xml_entity(uint32_t codepoint)
{
    static const char *const NAMED_ENTITIES[][2] = {
        { "&amp;", "&"},
        { "&gt;", ">" },
        { "&lt;", "<" },
        { "&copy;", "©" },
        { "&quot;", "\"" },
        { "&reg", "®" },
        { "&apos;", "'" },
        { 0, 0 }
    };

    int index = 0; while (NAMED_ENTITIES[index][0]) {
        if (codepoint == *NAMED_ENTITIES[index][1])
            return NAMED_ENTITIES[index][0];
        ++index;
    }

    static char XML_CHAR[32];
    if (codepoint > 0xff)
        sprintf(XML_CHAR, "&#%04X;", codepoint);
    else
        sprintf(XML_CHAR, "%c", codepoint);

    return XML_CHAR;
}

void print_glyph(FILE * file, texture_glyph_t * glyph)
{
    // TextureFont
    fprintf( file, "  {%u, ", glyph->codepoint );
    fprintf( file, "%" PRIzu ", %" PRIzu ", ", glyph->width, glyph->height );
    fprintf( file, "%d, %d, ", glyph->offset_x, glyph->offset_y );
    fprintf( file, "%ff, %ff, ", glyph->advance_x, glyph->advance_y );
    fprintf( file, "%ff, %ff, %ff, %ff, ", glyph->s0, glyph->t0, glyph->s1, glyph->t1 );
    fprintf( file, "%" PRIzu ", ", vector_size(glyph->kerning) );
    if (vector_size(glyph->kerning) == 0) {
        fprintf( file, "0" );
    } else {
        int k;
        fprintf( file, "{ " );
        for( k=0; k < vector_size(glyph->kerning); ++k ) {
            float *kerning = *(float **) vector_get( glyph->kerning, k);
            int l;
            fprintf( file, "{" );
            for( l=0; l<0xff; l++ )
                fprintf( file, " %ff,", kerning[l] );
            fprintf( file, " %ff }", kerning[0xFF] );

            if( k < (vector_size(glyph->kerning)-1))
                fprintf( file, ",\n" );
        }
        fprintf( file, " }" );
    }
    fprintf( file, " };\n" );
}

// ------------------------------------------------------------------- main ---
int main( int argc, char **argv )
{
    FILE* test;
    size_t i, j, k;
    texture_glyph_t * glyph;
    int arg;

    const char * font_cache =
        " !\"#$%&'()*+,-./0123456789:;<=>?"
        "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
        "`abcdefghijklmnopqrstuvwxyz{|}~";

    float font_size = 0.0;
    const char * font_filename   = NULL;
    const char * header_filename = NULL;
    const char * variable_name   = "font";
    int show_help = 0;
    size_t texture_width = 0;
    int depth = 1;
    float padding[4] = {0,0,0,0}; // left,right,top,bottom
    size_t spacing = 0;
    rendermode_t rendermode = RENDER_NORMAL;
    const char *rendermodes[5];
    rendermodes[RENDER_NORMAL] = "normal";
    rendermodes[RENDER_OUTLINE_EDGE] = "outline edge";
    rendermodes[RENDER_OUTLINE_POSITIVE] = "outline added";
    rendermodes[RENDER_OUTLINE_NEGATIVE] = "outline removed";
    rendermodes[RENDER_SIGNED_DISTANCE_FIELD] = "signed distance field";

    for ( arg = 1; arg < argc; ++arg )
    {
        if ( 0 == strcmp( "--font", argv[arg] ) || 0 == strcmp( "-f", argv[arg] ) )
        {
            ++arg;

            if ( font_filename )
            {
                fprintf( stderr, "Multiple --font parameters.\n" );
                print_help();
                exit( 1 );
            }

            if ( arg >= argc )
            {
                fprintf( stderr, "No font file given.\n" );
                print_help();
                exit( 1 );
            }

            font_filename = argv[arg];
            continue;
        }

        if ( 0 == strcmp( "--header", argv[arg] ) || 0 == strcmp( "-o", argv[arg] )  )
        {
            ++arg;

            if ( header_filename )
            {
                fprintf( stderr, "Multiple --header parameters.\n" );
                print_help();
                exit( 1 );
            }

            if ( arg >= argc )
            {
                fprintf( stderr, "No header file given.\n" );
                print_help();
                exit( 1 );
            }

            header_filename = argv[arg];
            continue;
        }

        if ( 0 == strcmp( "--help", argv[arg] ) || 0 == strcmp( "-h", argv[arg] ) )
        {
            show_help = 1;
            break;
        }

        if ( 0 == strcmp( "--size", argv[arg] ) || 0 == strcmp( "-s", argv[arg] ) )
        {
            ++arg;

            if ( 0.0 != font_size )
            {
                fprintf( stderr, "Multiple --size parameters.\n" );
                print_help();
                exit( 1 );
            }

            if ( arg >= argc )
            {
                fprintf( stderr, "No font size given.\n" );
                print_help();
                exit( 1 );
            }

            errno = 0;

            font_size = atof( argv[arg] );

            if ( errno )
            {
                fprintf( stderr, "No valid font size given.\n" );
                print_help();
                exit( 1 );
            }

            continue;
        }

        if ( 0 == strcmp( "--variable", argv[arg] ) || 0 == strcmp( "-a", argv[arg] )  )
        {
            ++arg;

            if ( 0 != strcmp( "font", variable_name ) )
            {
                fprintf( stderr, "Multiple --variable parameters.\n" );
                print_help();
                exit( 1 );
            }

            if ( arg >= argc )
            {
                fprintf( stderr, "No variable name given.\n" );
                print_help();
                exit( 1 );
            }

            variable_name = argv[arg];
            continue;
        }

        if ( 0 == strcmp( "--texture", argv[arg] ) || 0 == strcmp( "-t", argv[arg] ) )
        {
            ++arg;

            if ( 0 != texture_width )
            {
                fprintf( stderr, "Multiple --texture parameters.\n" );
                print_help();
                exit( 1 );
            }

            if ( arg >= argc )
            {
                fprintf( stderr, "No texture size given.\n" );
                print_help();
                exit( 1 );
            }

            errno = 0;

            texture_width = atof( argv[arg] );

            if ( errno )
            {
                fprintf( stderr, "No valid texture size given.\n" );
                print_help();
                exit( 1 );
            }

            continue;
        }

        if ( 0 == strcmp( "--padding", argv[arg] ) || 0 == strcmp( "-p", argv[arg] ) )
        {
            ++arg;

            if ( 0 != padding[0] || 0 != padding[1] || 0 != padding[2] || 0 != padding[3] )
            {
                fprintf( stderr, "Multiple --padding parameters.\n" );
                print_help();
                exit( 1 );
            }

            if ( arg >= argc )
            {
                fprintf( stderr, "No padding value given.\n" );
                print_help();
                exit( 1 );
            }

            int nb = 0; char *p = argv[arg]; while (nb < sizeof(padding) && *p) {
                padding[nb++] = strtol(p, &p, 10);
                if ( 0 == padding[nb] && errno == EINVAL )
                {
                    fprintf( stderr, "No valid padding value given.\n" );
                    print_help();
                    exit( 1 );
                }
                if (*p != ',')
                    break;
                p++; /* skip, */
            }

            continue;
        }

        if ( 0 == strcmp( "--spacing", argv[arg] ) || 0 == strcmp( "-sp", argv[arg] ) )
        {
            ++arg;

            if ( 0 != spacing )
            {
                fprintf( stderr, "Multiple --spacing parameters.\n" );
                print_help();
                exit( 1 );
            }

            if ( arg >= argc )
            {
                fprintf( stderr, "No spacing value given.\n" );
                print_help();
                exit( 1 );
            }

            errno = 0;

            spacing = atoi( argv[arg] );

            if ( errno )
            {
                fprintf( stderr, "No valid spacing value given.\n" );
                print_help();
                exit( 1 );
            }

            continue;
        }

        if ( 0 == strcmp( "--rendermode", argv[arg] ) || 0 == strcmp( "-r", argv[arg] ) )
        {
            ++arg;

            if ( arg >= argc )
            {
                fprintf( stderr, "No render mode given.\n" );
                print_help();
                exit( 1 );
            }

            errno = 0;

            if( 0 == strcmp( "normal", argv[arg] ) )
            {
                rendermode = RENDER_NORMAL;
            }
            else if( 0 == strcmp( "outline_edge", argv[arg] ) )
            {
                rendermode = RENDER_OUTLINE_EDGE;
            }
            else if( 0 == strcmp( "outline_positive", argv[arg] ) )
            {
                rendermode = RENDER_OUTLINE_POSITIVE;
            }
            else if( 0 == strcmp( "outline_negative", argv[arg] ) )
            {
                rendermode = RENDER_OUTLINE_NEGATIVE;
            }
            else if( 0 == strcmp( "sdf", argv[arg] ) )
            {
                rendermode = RENDER_SIGNED_DISTANCE_FIELD;
            }
            else
            {
                fprintf( stderr, "No valid render mode given.\n" );
                print_help();
                exit( 1 );
            }

            continue;
        }

        fprintf( stderr, "Unknown parameter %s\n", argv[arg] );
        print_help();
        exit( 1 );
    }

    if ( show_help )
    {
        print_help();
        exit( 1 );
    }

    if ( !font_filename )
    {
        fprintf( stderr, "No font file given.\n" );
        print_help();
        exit( 1 );
    }

    if ( !( test = fopen( font_filename, "r" ) ) )
    {
        fprintf( stderr, "Font file \"%s\" does not exist.\n", font_filename );
    }

    fclose( test );

    if ( 0 < font_size && 4.0 > font_size )
    {
        fprintf( stderr, "Font size too small, expected at least 4 pt.\n" );
        print_help();
        exit( 1 );
    }

    if ( !header_filename )
    {
        fprintf( stderr, "No header file given.\n" );
        print_help();
        exit( 1 );
    }

    if ( 0 == texture_width )
    {
        texture_width = 128;
    }

    int auto_size = font_size < 0;
    float font_size_step = 16;
    if (auto_size)
        font_size = 4;
    size_t missed = 0, last_missed = 0;
    texture_font_t  * font = 0;

#define delete_font(fnt) texture_font_delete(fnt); fnt = 0;

    texture_atlas_t * atlas = texture_atlas_new( texture_width, texture_width, depth );
    if ( 0 != spacing)
        atlas->spacing_horiz = atlas->spacing_vert = spacing;
    while(!font || auto_size) {
        texture_atlas_clear(atlas);
        font  = texture_font_new_from_file( atlas, font_size, font_filename );
        for (int i = 0; i < 4; ++i)
            if ( 0 != padding[i])
                switch (i) {
                    case 0: font->padding_left = padding[i]; break;
                    case 1: font->padding_right = padding[i]; break;
                    case 2: font->padding_top = padding[i]; break;
                    case 3: font->padding_bottom = padding[i]; break;
                }
        font->rendermode = rendermode;

        missed = texture_font_load_glyphs( font, font_cache );

        if (auto_size) {
            // first run failed
            if (missed && font_size == 4)
            {
                fprintf( stderr, "Texture too small to fit all characters.\n" );
                exit( 1 );
            }
            // found perfect size
            else if (missed && last_missed == 0 && font_size_step <= 1)
            {
                auto_size = 0;
                font_size -= font_size_step; // previous font size
            }
            else if (missed && font_size_step > 1)
            {
                font_size_step /= 2;
                font_size -= font_size_step;
            }
            else if (missed)
            {
                font_size -= font_size_step;
            }
            else if (missed == 0)
            {
                font_size += font_size_step;
            }
            delete_font(font);
            last_missed = missed;
        }
    }

    if(font == NULL)
    {
        fprintf( stderr, "Font not generated.\n" );
        exit( 1 );
    }

    printf( "Font filename           : %s\n"
            "Font size               : %.1f\n"
            "Padding                 : %.1f,%.1f,%.1f,%.1f\n"
            "Number of req. glyphs   : %" PRIzu "\n"
            "Number of glyphs        : %" PRIzu "\n"
            "Number of missed glyphs : %" PRIzu "\n"
            "Texture size            : %ldx%ldx%ld\n"
            "Spacing                 : %" PRIzu ",%" PRIzu "\n"
            "Texture occupancy       : %.2f%%\n"
            "\n"
            "Header filename         : %s\n"
            "Variable name           : %s\n"
            "Render mode             : %s\n",
            font_filename,
            font_size,
            font->padding_left, font->padding_right, font->padding_top, font->padding_bottom,
            strlen(font_cache),
            vector_glyphs_size( font->glyphs ),
            missed,
            atlas->width, atlas->height, atlas->depth,
            atlas->spacing_horiz, atlas->spacing_vert,
            100.0 * atlas->used / (float)(atlas->width * atlas->height),
            header_filename,
            variable_name,
            rendermodes[rendermode] );

    const size_t texture_size = atlas->width * atlas->height * atlas->depth;
    const size_t glyph_count = vector_size( font->glyphs );
    size_t max_kerning_count = 1;
    for( i=0; i < glyph_count; ++i )
    {
        texture_glyph_t **glyph_0x100 = *(texture_glyph_t ***) vector_get( font->glyphs, i );
        if(glyph_0x100) {
            for( j=0; j < 0x100; ++j ) {
                texture_glyph_t *glyph;
                if(( glyph = glyph_0x100[j] )) {
                    size_t new_max = vector_size(glyph->kerning);
                    if( new_max > max_kerning_count )
                        max_kerning_count = new_max;
                }
            }
        }
    }



    // -------------
    // Headers
    // -------------

    // ------------------
    // Dump texture image
    // ------------------
    char image_filename[PATH_MAX]; strncpy(image_filename, header_filename, PATH_MAX);
    char *ext = strrchr(image_filename, '.');
    if (ext != NULL)
        strcpy(ext, ".png");
    else
        strcat(image_filename, ".png");

    dumpimage(atlas->data, atlas->width, atlas->height, atlas->depth, image_filename);


    // BEGIN BMFONT

    // File tags
    // =========
    //
    // info
    // ----
    //
    // This tag holds information on how the font was generated.
    //
    // face     This is the name of the true type font.
    // size     The size of the true type font.
    // bold     The font is bold.
    // italic   The font is italic.
    // charset  The name of the OEM charset used (when not unicode).
    // unicode  Set to 1 if it is the unicode charset.
    // stretchH The font height stretch in percentage. 100% means no stretch.
    // smooth   Set to 1 if smoothing was turned on.
    // aa       The supersampling level used. 1 means no supersampling was used.
    // padding  The padding for each character (up, right, down, left).
    // spacing  The spacing for each character (horizontal, vertical).
    // outline  The outline thickness for the characters.
    //
    // common
    // ------
    //
    // This tag holds information common to all characters.
    //
    // lineHeight   This is the distance in pixels between each line of text.
    // base         The number of pixels from the absolute top of the line to the base of the characters.
    // scaleW       The width of the texture, normally used to scale the x pos of the character image.
    // scaleH       The height of the texture, normally used to scale the y pos of the character image.
    // pages        The number of texture pages included in the font.
    // packed       Set to 1 if the monochrome characters have been packed into each of the texture channels. In this case alphaChnl describes what is stored in each channel.
    // alphaChnl    Set to 0 if the channel holds the glyph data, 1 if it holds the outline, 2 if it holds the glyph and the outline, 3 if its set to zero, and 4 if its set to one.
    // redChnl      Set to 0 if the channel holds the glyph data, 1 if it holds the outline, 2 if it holds the glyph and the outline, 3 if its set to zero, and 4 if its set to one.
    // greenChnl    Set to 0 if the channel holds the glyph data, 1 if it holds the outline, 2 if it holds the glyph and the outline, 3 if its set to zero, and 4 if its set to one.
    // blueChnl     Set to 0 if the channel holds the glyph data, 1 if it holds the outline, 2 if it holds the glyph and the outline, 3 if its set to zero, and 4 if its set to one.
    //
    // page
    // ----
    //
    // This tag gives the name of a texture file. There is one for each page in the font.
    //
    // id   The page id.
    // file The texture file name.
    //
    // char
    // ----
    //
    // This tag describes on character in the font. There is one for each included character in the font.
    //
    // id       The character id.
    // x        The left position of the character image in the texture.
    // y        The top position of the character image in the texture.
    // width    The width of the character image in the texture.
    // height   The height of the character image in the texture.
    // xoffset  How much the current position should be offset when copying the image from the texture to the screen.
    // yoffset  How much the current position should be offset when copying the image from the texture to the screen.
    // xadvance How much the current position should be advanced after drawing the character.
    // page     The texture page where the character image is found.
    // chnl     The texture channel where the character image is found (1 = blue, 2 = green, 4 = red, 8 = alpha, 15 = all channels).
    //
    // kerning
    // -------
    //
    // The kerning information is used to adjust the distance between certain characters, e.g. some characters should be placed closer to each other than others.
    //
    // first    The first character id.
    // second   The second character id.
    // amount   How much the x position should be adjusted when drawing the second character immediately following the first.

    // <?xml version="1.0"?>
    // <font>
    // <info face="HGE font" size="32" bold="0" italic="0" charset="" unicode="0" stretchH="100" smooth="1" aa="1" padding="0,0,0,0" spacing="2,2" />
    // <common lineHeight="27" base="27" scaleW="256" scaleH="136" pages="1" packed="0" />
    // <pages>
    //   <page id="0" file="HGE_font1.png" />
    // </pages>
    // <chars count="95" >
    //   <char id="32" x="0" y="0" width="8" height="27" xoffset="0" yoffset="0" xadvance="8" page="0" chnl="0" letter=" " />
    //   <char id="33" x="8" y="0" width="5" height="27" xoffset="0" yoffset="0" xadvance="5" page="0" chnl="0" letter="!" />
    //   ...
    // </chars>
    // </font>

#define roundi(x) (int)round(x)

    char bmf_header_filename[PATH_MAX]; strncpy(bmf_header_filename, header_filename, PATH_MAX);
    char *dot = strrchr(bmf_header_filename, '.');
    if(!dot || dot == bmf_header_filename)
        strcat(bmf_header_filename, ".fnt");
    else
        strcpy(dot, ".fnt");

    FILE *bfile = fopen( bmf_header_filename, "w" );

    const char *base_font_file = strrchr( font_filename, '/' );

    fprintf( bfile, "<?xml version=\"1.0\"?>\n<font>\n" );
    fprintf( bfile, "<info face=\"%s\" size=\"%d\" bold=\"0\" italic=\"0\" charset=\"\" unicode=\"0\" stretchH=\"100\" smooth=\"1\" aa=\"1\" padding=\"%d,%d,%d,%d\" spacing=\"%" PRIzu ",%" PRIzu "\" />\n",
        font->family ? font->family : base_font_file ? base_font_file+1 : font_filename,
        roundi(font->size),
        roundi(font->padding_left),
        roundi(font->padding_right),
        roundi(font->padding_top),
        roundi(font->padding_bottom),
        atlas->spacing_horiz,
        atlas->spacing_vert
    );
    fprintf( bfile, "<common lineHeight=\"%d\" base=\"%d\" scaleW=\"%" PRIzu "\" scaleH=\"%" PRIzu "\" pages=\"1\" packed=\"0\" />\n",
        roundi(font->ascender - font->descender /* + font->linegap */),
        roundi(font->ascender),
        atlas->width,
        atlas->height
    );

    fprintf( bfile, "<pages>\n" );
    fprintf( bfile, "  <page id=\"0\" file=\"%s\" />\n", image_filename );
    fprintf( bfile, "</pages>\n" );

    // --------------
    // Texture glyphs
    // --------------
    fprintf( bfile, "<chars count=\"%" PRIzu "\">\n", glyph_count );
    for( i=0; i < glyph_count; ++i )
    {
        texture_glyph_t **glyph_0x100 = *(texture_glyph_t ***) vector_get( font->glyphs, i );
        if (glyph_0x100)
        {
            for( j=0; j < 0x100; ++j ) {
                texture_glyph_t *glyph;
                if(( glyph = glyph_0x100[j] )) {
                    fprintf( bfile, "  <char id=\"%u\" code=\"%u\" x=\"%" PRIzu "\" y=\"%" PRIzu "\" width=\"%" PRIzu "\" height=\"%" PRIzu "\" data_x=\"%" PRIzu "\" data_y=\"%" PRIzu "\" data_width=\"%" PRIzu "\" data_height=\"%" PRIzu "\" xoffset=\"%d\" yoffset=\"%d\" xadvance=\"%d\" page=\"0\" chnl=\"0\" letter=\"%s\"/>\n",
                        glyph->codepoint, glyph->codepoint,
                        glyph->x, glyph->y, glyph->width, glyph->height,
                        glyph->data_x, glyph->data_y, glyph->data_width, glyph->data_height,
                        roundi(glyph->offset_x), roundi(font->ascender - glyph->offset_y),
                        roundi(glyph->advance_x),
                        xml_entity(glyph->codepoint)
                    );
                }
            }
        }
    }
    fprintf( bfile, "</chars>\n</font>\n" );

    fclose(bfile);

    // END BMFONT


    FILE *file = fopen( header_filename, "w" );

    // BEGIN C_HDR

    fprintf( file,
        "/* ============================================================================\n"
        " * Freetype GL - A C OpenGL Freetype engine\n"
        " * Platform:    Any\n"
        " * WWW:         https://github.com/rougier/freetype-gl\n"
        " * ----------------------------------------------------------------------------\n"
        " * Copyright 2011,2012 Nicolas P. Rougier. All rights reserved.\n"
        " *\n"
        " * Redistribution and use in source and binary forms, with or without\n"
        " * modification, are permitted provided that the following conditions are met:\n"
        " *\n"
        " *  1. Redistributions of source code must retain the above copyright notice,\n"
        " *     this list of conditions and the following disclaimer.\n"
        " *\n"
        " *  2. Redistributions in binary form must reproduce the above copyright\n"
        " *     notice, this list of conditions and the following disclaimer in the\n"
        " *     documentation and/or other materials provided with the distribution.\n"
        " *\n"
        " * THIS SOFTWARE IS PROVIDED BY NICOLAS P. ROUGIER ''AS IS'' AND ANY EXPRESS OR\n"
        " * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF\n"
        " * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO\n"
        " * EVENT SHALL NICOLAS P. ROUGIER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,\n"
        " * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES\n"
        " * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;\n"
        " * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND\n"
        " * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT\n"
        " * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF\n"
        " * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n"
        " *\n"
        " * The views and conclusions contained in the software and documentation are\n"
        " * those of the authors and should not be interpreted as representing official\n"
        " * policies, either expressed or implied, of Nicolas P. Rougier.\n"
        " * ============================================================================\n"
        " */\n\n");

    fprintf( file, 
        "/* ============================================================================\n"
        " * Parameters\n"
        " * ----------------------------------------------------------------------------\n"
        " * Font size: %f\n"
        " * Texture width: %" PRIzu "\n"
        " * Texture height: %" PRIzu "\n"
        " * Texture depth: %" PRIzu "\n"
        " * ===============================================================================\n"
        " */\n\n", 
        font_size, atlas->width, atlas->height, atlas->depth);


    // ----------------------
    // Structure declarations
    // ----------------------
    fprintf( file,
	     "#include <stddef.h>\n"
	     "#include <stdint.h>\n"
	     "#ifdef __cplusplus\n"
	     "extern \"C\" {\n"
	     "#endif\n"
	     "\n" );

    fprintf( file,
        "typedef struct\n"
        "{\n"
        "    uint32_t codepoint;\n"
        "    int width, height;\n"
        "    int offset_x, offset_y;\n"
        "    float advance_x, advance_y;\n"
        "    float s0, t0, s1, t1;\n"
        "    size_t kerning_count;\n"
        "    float kerning[%" PRIzu "][0x100];\n"
        "} texture_glyph_t;\n\n", max_kerning_count );

    fprintf( file,
	     "typedef struct\n"
	     "{\n"
	     "   texture_glyph_t *glyphs[0x100];\n"
	     "} texture_glyph_0x100_t;\n\n" );

    fprintf( file,
        "typedef struct\n"
        "{\n"
        "    size_t tex_width;\n"
        "    size_t tex_height;\n"
        "    size_t tex_depth;\n"
        "    unsigned char tex_data[%" PRIzu "];\n"
        "    float size;\n"
        "    float height;\n"
        "    float linegap;\n"
        "    float ascender;\n"
        "    float descender;\n"
        "    size_t glyphs_count;\n"
        "    texture_glyph_0x100_t glyphs[%" PRIzu "];\n"
        "} texture_font_t;\n\n", texture_size, glyph_count );

    GLYPHS_ITERATOR(i, glyph, font->glyphs) {
        fprintf( file, "texture_glyph_t %s_glyph_%08x = ", variable_name, glyph->codepoint );
        print_glyph(file, glyph);
    }
    GLYPHS_ITERATOR_END

    fprintf( file, "texture_font_t %s = {\n", variable_name );


    // ------------
    // Texture data
    // ------------
    fprintf( file, " %" PRIzu ", %" PRIzu ", %" PRIzu ",\n", atlas->width, atlas->height, atlas->depth );
    fprintf( file, " {" );
    for( i=0; i < texture_size; i+=32 )
    {
        for( j=0; j < 32 && (j+i) < texture_size ; ++ j)
        {
            if( (j+i) < (texture_size-1) )
            {
                fprintf( file, "%d,", atlas->data[i+j] );
            }
            else
            {
                fprintf( file, "%d", atlas->data[i+j] );
            }
        }
        if( (j+i) < texture_size )
        {
            fprintf( file, "\n  " );
        }
    }
    fprintf( file, "}, \n" );


    // -------------------
    // Texture information
    // -------------------
    fprintf( file, " %ff, %ff, %ff, %ff, %ff, %" PRIzu ", \n",
            font->size, font->height,
            font->linegap,font->ascender, font->descender,
            glyph_count );

    // --------------
    // Texture glyphs
    // --------------
    fprintf( file, " {\n" );
    GLYPHS_ITERATOR1(i, glyph, font->glyphs) {
        fprintf( file, " {\n" );
        GLYPHS_ITERATOR2(i, glyph, font->glyphs) {
            fprintf( file, "  &%s_glyph_%08x,\n", variable_name, glyph->codepoint );
        } else {
            fprintf( file, "  NULL,\n" );
        }
        GLYPHS_ITERATOR_END1;
        fprintf( file, " },\n" );
    } GLYPHS_ITERATOR_END2;
    fprintf( file, " }\n};\n" );
    fprintf( file,
        "#ifdef __cplusplus\n"
        "}\n"
        "#endif\n" );

    fclose( file );

    return 0;
}


// Minimal C library to create uncompressed PNG images
// ---------------------------------------------------

// This struct holds the internal state of the PNG. The members should never be used directly.
struct libattopng_t {
    libattopng_type_t type;      /**< File type */
    size_t capacity;             /**< Reserved memory for raw data */
    char *data;                  /**< Raw pixel data, format depends on type */
    uint32_t *palette;           /**< Palette for image */
    size_t palette_length;       /**< Entries for palette, 0 if unused */
    size_t width;                /**< Image width */
    size_t height;               /**< Image height */

    char *out;                   /**< Buffer to store final PNG */
    size_t out_pos;              /**< Current size of output buffer */
    size_t out_capacity;         /**< Capacity of output buffer */
    uint32_t crc;                /**< Currecnt CRC32 checksum */
    uint16_t s1;                 /**< Helper variables for Adler checksum */
    uint16_t s2;                 /**< Helper variables for Adler checksum */
    size_t bpp;                  /**< Bytes per pixel */

    size_t stream_x;             /**< Current x coordinate for pixel streaming */
    size_t stream_y;             /**< Current y coordinate for pixel streaming */
};

#define LIBATTOPNG_ADLER_BASE 65521

static const uint32_t libattopng_crc32[256] = {
        0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3, 0x0edb8832,
        0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
        0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7, 0x136c9856, 0x646ba8c0, 0xfd62f97a,
        0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
        0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3,
        0x45df5c75, 0xdcd60dcf, 0xabd13d59, 0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
        0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab,
        0xb6662d3d, 0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
        0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01, 0x6b6b51f4,
        0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
        0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65, 0x4db26158, 0x3ab551ce, 0xa3bc0074,
        0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
        0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525,
        0x206f85b3, 0xb966d409, 0xce61e49f, 0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
        0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615,
        0x73dc1683, 0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
        0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7, 0xfed41b76,
        0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
        0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b, 0xd80d2bda, 0xaf0a1b4c, 0x36034af6,
        0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
        0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7,
        0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d, 0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
        0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7,
        0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
        0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45, 0xa00ae278,
        0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
        0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9, 0xbdbdf21c, 0xcabac28a, 0x53b39330,
        0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
        0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

/* ------------------------------------------------------------------------ */
libattopng_t *libattopng_new(size_t width, size_t height, libattopng_type_t type) {
    libattopng_t *png;
    if (SIZE_MAX / 4 / width < height) {
        /* ensure no type leads to an integer overflow */
        return NULL;
    }
    png = (libattopng_t *) malloc(sizeof(libattopng_t));
    png->width = width;
    png->height = height;
    png->capacity = width * height;
    png->palette_length = 0;
    png->palette = NULL;
    png->out = NULL;
    png->out_capacity = 0;
    png->out_pos = 0;
    png->type = type;
    png->stream_x = 0;
    png->stream_y = 0;

    if (type == PNG_PALETTE) {
        png->palette = (uint32_t *) calloc(256, sizeof(uint32_t));
        if (!png->palette) {
            free(png);
            return NULL;
        }
        png->bpp = 1;
    } else if (type == PNG_GRAYSCALE) {
        png->bpp = 1;
    } else if (type == PNG_GRAYSCALE_ALPHA) {
        png->capacity *= 2;
        png->bpp = 2;
    } else if (type == PNG_RGB) {
        png->capacity *= 4;
        png->bpp = 3;
    } else if (type == PNG_RGBA) {
        png->capacity *= 4;
        png->bpp = 4;
    }

    png->data = (char *) malloc(png->capacity);
    if (!png->data) {
        free(png->palette);
        free(png);
        return NULL;
    }
    return png;
}

/* ------------------------------------------------------------------------ */
int libattopng_set_palette(libattopng_t *png, uint32_t *palette, size_t length) {
    if (length > 256) {
        return 1;
    }
    memmove(png->palette, palette, length * sizeof(uint32_t));
    png->palette_length = length;
    return 0;
}

/* ------------------------------------------------------------------------ */
void libattopng_set_pixel(libattopng_t *png, size_t x, size_t y, uint32_t color) {
    if (!png || x >= png->width || y >= png->height) {
        return;
    }
    if (png->type == PNG_PALETTE || png->type == PNG_GRAYSCALE) {
        png->data[x + y * png->width] = (char) (color & 0xff);
    } else if (png->type == PNG_GRAYSCALE_ALPHA) {
        ((uint16_t *) png->data)[x + y * png->width] = (uint16_t) (color & 0xffff);
    } else {
        ((uint32_t *) png->data)[x + y * png->width] = color;
    }
}

/* ------------------------------------------------------------------------ */
uint32_t libattopng_get_pixel(libattopng_t* png, size_t x, size_t y) {
    uint32_t pixel = 0;
    if (!png || x >= png->width || y >= png->height) {
        return pixel;
    }
    if (png->type == PNG_PALETTE || png->type == PNG_GRAYSCALE) {
        pixel = (uint32_t)(png->data[x + y * png->width] & 0xff);
    } else if (png->type == PNG_GRAYSCALE_ALPHA) {
        pixel = (uint32_t)(((uint16_t *) png->data)[x + y * png->width] & 0xffff);
    } else {
        pixel = ((uint32_t *) png->data)[x + y * png->width];
    }
    return pixel;
}

/* ------------------------------------------------------------------------ */
void libattopng_start_stream(libattopng_t* png, size_t x, size_t y) {
    if(!png || x >= png->width || y >= png->height) {
        return;
    }
    png->stream_x = x;
    png->stream_y = y;
}

/* ------------------------------------------------------------------------ */
void libattopng_put_pixel(libattopng_t* png, uint32_t color) {
    size_t x, y;
    if(!png) {
        return;
    }
    x = png->stream_x;
    y = png->stream_y;
    if (png->type == PNG_PALETTE || png->type == PNG_GRAYSCALE) {
        png->data[x + y * png->width] = (char) (color & 0xff);
    } else if (png->type == PNG_GRAYSCALE_ALPHA) {
        ((uint16_t *) png->data)[x + y * png->width] = (uint16_t) (color & 0xffff);
    } else {
        ((uint32_t *) png->data)[x + y * png->width] = color;
    }
    x++;
    if(x >= png->width) {
        x = 0;
        y++;
        if(y >= png->height) {
            y = 0;
        }
    }
    png->stream_x = x;
    png->stream_y = y;
}

/* ------------------------------------------------------------------------ */
static uint32_t libattopng_swap32(uint32_t num) {
    return ((num >> 24) & 0xff) |
           ((num << 8) & 0xff0000) |
           ((num >> 8) & 0xff00) |
           ((num << 24) & 0xff000000);
}

/* ------------------------------------------------------------------------ */
static uint32_t libattopng_crc(const unsigned char *data, size_t len, uint32_t crc) {
    for (size_t i = 0; i < len; i++) {
        crc = libattopng_crc32[(crc ^ data[i]) & 255] ^ (crc >> 8);
    }
    return crc;
}

/* ------------------------------------------------------------------------ */
static void libattopng_out_raw_write(libattopng_t *png, const char *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        png->out[png->out_pos++] = data[i];
    }
}

/* ------------------------------------------------------------------------ */
static void libattopng_out_raw_uint(libattopng_t *png, uint32_t val) {
    *(uint32_t *) (png->out + png->out_pos) = val;
    png->out_pos += 4;
}

/* ------------------------------------------------------------------------ */
static void libattopng_out_raw_uint16(libattopng_t *png, uint16_t val) {
    *(uint16_t *) (png->out + png->out_pos) = val;
    png->out_pos += 2;
}

/* ------------------------------------------------------------------------ */
static void libattopng_out_raw_uint8(libattopng_t *png, uint8_t val) {
    *(uint8_t *) (png->out + png->out_pos) = val;
    png->out_pos++;
}

/* ------------------------------------------------------------------------ */
static void libattopng_new_chunk(libattopng_t *png, const char *name, size_t len) {
    png->crc = 0xffffffff;
    libattopng_out_raw_uint(png, libattopng_swap32((uint32_t) len));
    png->crc = libattopng_crc((const unsigned char *) name, 4, png->crc);
    libattopng_out_raw_write(png, name, 4);
}

/* ------------------------------------------------------------------------ */
static void libattopng_end_chunk(libattopng_t *png) {
    libattopng_out_raw_uint(png, libattopng_swap32(~png->crc));
}

/* ------------------------------------------------------------------------ */
static void libattopng_out_uint32(libattopng_t *png, uint32_t val) {
    png->crc = libattopng_crc((const unsigned char *) &val, 4, png->crc);
    libattopng_out_raw_uint(png, val);
}

/* ------------------------------------------------------------------------ */
static void libattopng_out_uint16(libattopng_t *png, uint16_t val) {
    png->crc = libattopng_crc((const unsigned char *) &val, 2, png->crc);
    libattopng_out_raw_uint16(png, val);
}

/* ------------------------------------------------------------------------ */
static void libattopng_out_uint8(libattopng_t *png, uint8_t val) {
    png->crc = libattopng_crc((const unsigned char *) &val, 1, png->crc);
    libattopng_out_raw_uint8(png, val);
}

/* ------------------------------------------------------------------------ */
static void libattopng_out_write(libattopng_t *png, const char *data, size_t len) {
    png->crc = libattopng_crc((const unsigned char *) data, len, png->crc);
    libattopng_out_raw_write(png, data, len);
}

/* ------------------------------------------------------------------------ */
static void libattopng_out_write_adler(libattopng_t *png, unsigned char data) {
    libattopng_out_write(png, (char *) &data, 1);
    png->s1 = (uint16_t) ((png->s1 + data) % LIBATTOPNG_ADLER_BASE);
    png->s2 = (uint16_t) ((png->s2 + png->s1) % LIBATTOPNG_ADLER_BASE);
}

/* ------------------------------------------------------------------------ */
static void libattopng_pixel_header(libattopng_t *png, size_t offset, size_t bpl) {
    if (offset > bpl) {
        /* not the last line */
        libattopng_out_write(png, "\0", 1);
        libattopng_out_uint16(png, (uint16_t) bpl);
        libattopng_out_uint16(png, (uint16_t) ~bpl);
    } else {
        /* last line */
        libattopng_out_write(png, "\1", 1);
        libattopng_out_uint16(png, (uint16_t) offset);
        libattopng_out_uint16(png, (uint16_t) ~offset);
    }
}

/* ------------------------------------------------------------------------ */
char *libattopng_get_data(libattopng_t *png, size_t *len) {
    size_t index, bpl, raw_size, size, p, pos, corr;
    unsigned char *pixel;
    if (!png) {
        return NULL;
    }
    if (png->out) {
        /* delete old output if any */
        free(png->out);
    }
    png->out_capacity = png->capacity + 4096 * 8;
    png->out = (char *) malloc(png->out_capacity);
    png->out_pos = 0;
    if (!png->out) {
        return NULL;
    }

    libattopng_out_raw_write(png, "\211PNG\r\n\032\n", 8);

    /* IHDR */
    libattopng_new_chunk(png, "IHDR", 13);
    libattopng_out_uint32(png, libattopng_swap32((uint32_t) (png->width)));
    libattopng_out_uint32(png, libattopng_swap32((uint32_t) (png->height)));
    libattopng_out_uint8(png, 8); /* bit depth */
    libattopng_out_uint8(png, (uint8_t) png->type);
    libattopng_out_uint8(png, 0); /* compression */
    libattopng_out_uint8(png, 0); /* filter */
    libattopng_out_uint8(png, 0); /* interlace method */
    libattopng_end_chunk(png);

    /* palette */
    if (png->type == PNG_PALETTE) {
        char entry[3];
        size_t s = png->palette_length;
        if (s < 16) {
            s = 16; /* minimum palette length */
        }
        libattopng_new_chunk(png, "PLTE", 3 * s);
        for (index = 0; index < s; index++) {
            entry[0] = (char) (png->palette[index] & 255);
            entry[1] = (char) ((png->palette[index] >> 8) & 255);
            entry[2] = (char) ((png->palette[index] >> 16) & 255);
            libattopng_out_write(png, entry, 3);
        }
        libattopng_end_chunk(png);

        /* transparency */
        libattopng_new_chunk(png, "tRNS", s);
        for (index = 0; index < s; index++) {
            entry[0] = (char) ((png->palette[index] >> 24) & 255);
            libattopng_out_write(png, entry, 1);
        }
        libattopng_end_chunk(png);
    }

    /* data */
    bpl = 1 + png->bpp * png->width;
    raw_size = png->height * bpl;
    size = 2 + png->height * (5 + bpl) + 4;
    libattopng_new_chunk(png, "IDAT", size);
    libattopng_out_write(png, "\170\332", 2);

    pixel = (unsigned char *) png->data;
    png->s1 = 1;
    png->s2 = 0;
    index = 0;
    if (png->type == PNG_RGB) {
        corr = 1;
    } else {
        corr = 0;
    }
    for (pos = 0; pos < png->width * png->height; pos++) {
        if (index == 0) {
            /* line header */
            libattopng_pixel_header(png, raw_size, bpl);
            libattopng_out_write_adler(png, 0); /* no filter */
            raw_size--;
        }

        /* pixel */
        for (p = 0; p < png->bpp; p++) {
            libattopng_out_write_adler(png, *pixel);
            pixel++;
        }
        pixel += corr;

        raw_size -= png->bpp;
        index = (index + 1) % png->width;
    }
    /* checksum */
    png->s1 %= LIBATTOPNG_ADLER_BASE;
    png->s2 %= LIBATTOPNG_ADLER_BASE;
    libattopng_out_uint32(png, libattopng_swap32((uint32_t) ((png->s2 << 16) | png->s1)));
    libattopng_end_chunk(png);

    /* end of image */
    libattopng_new_chunk(png, "IEND", 0);
    libattopng_end_chunk(png);

    if (len) {
        *len = png->out_pos;
    }
    return png->out;
}

/* ------------------------------------------------------------------------ */
int libattopng_save(libattopng_t *png, const char *filename) {
    size_t len;
    FILE* f;
    char *data = libattopng_get_data(png, &len);
    if (!data) {
        return 1;
    }
    f = fopen(filename, "wb");
    if (!f) {
        return 1;
    }
    if (fwrite(data, len, 1, f) != 1) {
        fclose(f);
        return 1;
    }
    fclose(f);
    return 0;
}

/* ------------------------------------------------------------------------ */
void libattopng_destroy(libattopng_t *png) {
    if (!png) {
        return;
    }
    free(png->palette);
    png->palette = NULL;
    free(png->out);
    png->out = NULL;
    free(png->data);
    png->data = NULL;
    free(png);
}

#ifdef __APPLE__
/*
 * The strrstr() function finds the last occurrence of the substring needle
 * in the string haystack. The terminating nul characters are not compared.
 */
char *strrstr(const char *haystack, const char *needle)
{
	char *r = NULL;

	if (!needle[0])
		return (char*)haystack + strlen(haystack);
	while (1) {
		char *p = strstr(haystack, needle);
		if (!p)
			return r;
		r = p;
		haystack = p + 1;
	}
}
#endif
