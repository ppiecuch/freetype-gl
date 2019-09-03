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


// ------------------------------------------------------------- print help ---
void print_help()
{
    fprintf( stderr, "Usage: makefont [--help] --font <font file> "
             "--header <header file> --size <font size> "
             "--variable <variable name> --texture <texture size> "
             "--rendermode <one of 'normal', 'outline_edge', 'outline_positive', 'outline_negative' or 'sdf'>\n" );
}


// ------------------------------------------------------------- dump image ---
void dumpimage( const char *buffer, const int width, const int height, const int depth, const char* path )
{
    FILE* out = fopen( path, "wb" );

    uint8_t *data = buffer; 
    uint8_t bpp = depth*8;
    if(depth == 1) { // upsacel from 8 to 16bit
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

    fwrite( tga_header, sizeof(uint8_t), 18, out );

    // save flipped data:
    const int chan = bpp / 8;
    const size_t lsize = width * chan;
    const uint8_t *data_end = data + width*height*chan - lsize;
    for(int y=0; y<height; y++)
    {
        fwrite( data_end - y*lsize, sizeof(uint8_t), lsize, out );
    }

    fclose( out );
    if(depth == 1)
        free( buffer );
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

    return 0;
}

// ------------------------------------------------------------------- main ---
int main( int argc, char **argv )
{
    FILE* test;
    size_t i, j;
    int arg;

    char * font_cache =
        " !\"#$%&'()*+,-./0123456789:;<=>?"
        "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
        "`abcdefghijklmnopqrstuvwxyz{|}~";

    float  font_size   = 0.0;
    const char * font_filename   = NULL;
    const char * header_filename = NULL;
    const char * variable_name   = "font";
    int show_help = 0;
    size_t texture_width = 128;
    float padding = 0;
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

            if ( 128.0 != texture_width )
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

            if ( 0 != padding )
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

            errno = 0;

            padding = atof( argv[arg] );

            if ( errno )
            {
                fprintf( stderr, "No valid padding value given.\n" );
                print_help();
                exit( 1 );
            }

            continue;
        }

        if ( 0 == strcmp( "--rendermode", argv[arg] ) || 0 == strcmp( "-r", argv[arg] ) )
        {
            ++arg;

            if ( 128.0 != texture_width )
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

    if ( 4.0 > font_size )
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

    texture_atlas_t * atlas = texture_atlas_new( texture_width, texture_width, 1 );
    texture_font_t  * font  = texture_font_new_from_file( atlas, font_size, font_filename );
    if ( 0 != padding)
        font->padding_left = font->padding_right = font->padding_top = font->padding_bottom = padding;
    font->rendermode = rendermode;

    size_t missed = texture_font_load_glyphs( font, font_cache );

    printf( "Font filename           : %s\n"
            "Font size               : %.1f\n"
            "Number of glyphs        : %ld\n"
            "Number of missed glyphs : %ld\n"
            "Texture size            : %ldx%ldx%ld\n"
            "Texture occupancy       : %.2f%%\n"
            "\n"
            "Header filename         : %s\n"
            "Variable name           : %s\n"
            "Render mode             : %s\n",
            font_filename,
            font_size,
            strlen(font_cache),
            missed,
            atlas->width, atlas->height, atlas->depth,
            100.0 * atlas->used / (float)(atlas->width * atlas->height),
            header_filename,
            variable_name,
            rendermodes[rendermode] );

    size_t texture_size = atlas->width * atlas->height *atlas->depth;
    size_t glyph_count = font->glyphs->size;
    size_t max_kerning_count = 1;
    for( i=0; i < glyph_count; ++i )
    {
        texture_glyph_t *glyph = *(texture_glyph_t **) vector_get( font->glyphs, i );

        if( vector_size(glyph->kerning) > max_kerning_count )
        {
            max_kerning_count = vector_size(glyph->kerning);
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
        strcpy(ext, ".tga");
    else
        strcat(image_filename, ".tga");

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
    fprintf( bfile, "<info face=\"%s\" size=\"%d\" bold=\"0\" italic=\"0\" charset=\"\" unicode=\"0\" stretchH=\"100\" smooth=\"1\" aa=\"1\" padding=\"%d,%d,%d,%d\" spacing=\"0,0\" />\n",
        base_font_file?base_font_file+1:font_filename,
        roundi(font->size),
        roundi(font->padding_left),
        roundi(font->padding_right),
        roundi(font->padding_top),
        roundi(font->padding_bottom)
    );
    fprintf( bfile, "<common lineHeight=\"%d\" base=\"27\" scaleW=\"256\" scaleH=\"136\" pages=\"1\" packed=\"0\" />\n",
        roundi(font->height)
    );

    fprintf( bfile, "<pages>\n" );
    fprintf( bfile, "  <page id=\"0\" file=\"%s\" />\n", image_filename );
    fprintf( bfile, "</pages>\n" );

    // --------------
    // Texture glyphs
    // --------------
    fprintf( bfile, "<chars count=\"%d\">\n", glyph_count );
    for( i=0; i < glyph_count; ++i )
    {
        texture_glyph_t * glyph = *(texture_glyph_t **) vector_get( font->glyphs, i );
        const char *xml_ent = xml_entity(glyph->codepoint);
        if (xml_ent)
            fprintf( bfile, "  <char id=\"%u\" x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" xoffset=\"%d\" yoffset=\"%d\" xadvance=\"%d\" page=\"0\" chnl=\"0\" letter=\"%s\"/>\n",
                glyph->codepoint,
                roundi(glyph->s0*atlas->width), roundi(glyph->t0*atlas->height),
                roundi(glyph->width), roundi(glyph->height),
                roundi(glyph->offset_x), roundi(glyph->offset_y),
                roundi(glyph->advance_x),
                xml_ent
            );
        else
            fprintf( bfile, "  <char id=\"%u\" x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" xoffset=\"%d\" yoffset=\"%d\" xadvance=\"%d\" page=\"0\" chnl=\"0\" letter=\"%lc\"/>\n",
                glyph->codepoint,
                roundi(glyph->s0*atlas->width), roundi(glyph->t0*atlas->height),
                roundi(glyph->width), roundi(glyph->height),
                roundi(glyph->offset_x), roundi(glyph->offset_y),
                roundi(glyph->advance_x),
                glyph->codepoint
            );
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
        " */\n");


    // ----------------------
    // Structure declarations
    // ----------------------
    fprintf( file,
        "#include <stddef.h>\n"
        "#include <stdint.h>\n"
        "#ifdef __cplusplus\n"
        "extern \"C\" {\n"
        "#endif\n"
        "\n"
        "typedef struct\n"
        "{\n"
        "    uint32_t codepoint;\n"
        "    float kerning;\n"
        "} kerning_t;\n\n" );

    fprintf( file,
        "typedef struct\n"
        "{\n"
        "    uint32_t codepoint;\n"
        "    int width, height;\n"
        "    int offset_x, offset_y;\n"
        "    float advance_x, advance_y;\n"
        "    float s0, t0, s1, t1;\n"
        "    size_t kerning_count;\n"
        "    kerning_t kerning[%" PRIzu "];\n"
        "} texture_glyph_t;\n\n", max_kerning_count );

    fprintf( file,
        "typedef struct\n"
        "{\n"
        "    size_t tex_width;\n"
        "    size_t tex_height;\n"
        "    size_t tex_depth;\n"
        "    char tex_data[%" PRIzu "];\n"
        "    float size;\n"
        "    float height;\n"
        "    float linegap;\n"
        "    float ascender;\n"
        "    float descender;\n"
        "    size_t glyphs_count;\n"
        "    texture_glyph_t glyphs[%" PRIzu "];\n"
        "} texture_font_t;\n\n", texture_size, glyph_count );



    fprintf( file, "texture_font_t %s = {\n", variable_name );


    // ------------
    // Texture data
    // ------------
    fprintf( file, " %" PRIzu ", %" PRIzu ", %" PRIzu ",\n", atlas->width, atlas->height, atlas->depth );
    fprintf( file, " {" );
    for( i=0; i < texture_size; i+= 32 )
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
    for( i=0; i < glyph_count; ++i )
    {
        texture_glyph_t * glyph = *(texture_glyph_t **) vector_get( font->glyphs, i );

/*
        // Debugging information
        printf( "glyph : '%lc'\n",
                 glyph->codepoint );
        printf( "  size       : %dx%d\n",
                 glyph->width, glyph->height );
        printf( "  offset     : %+d%+d\n",
                 glyph->offset_x, glyph->offset_y );
        printf( "  advance    : %ff, %ff\n",
                 glyph->advance_x, glyph->advance_y );
        printf( "  tex coords.: %ff, %ff, %ff, %ff\n",
                 glyph->u0, glyph->v0, glyph->u1, glyph->v1 );

        printf( "  kerning    : " );
        if( glyph->kerning_count )
        {
            for( j=0; j < glyph->kerning_count; ++j )
            {
                printf( "('%lc', %ff)",
                        glyph->kerning[j].codepoint, glyph->kerning[j].kerning );
                if( j < (glyph->kerning_count-1) )
                {
                    printf( ", " );
                }
            }
        }
        else
        {
            printf( "None" );
        }
        printf( "\n\n" );
*/

        // TextureFont
        fprintf( file, "  {%u, ", glyph->codepoint );
        fprintf( file, "%" PRIzu ", %" PRIzu ", ", glyph->width, glyph->height );
        fprintf( file, "%d, %d, ", glyph->offset_x, glyph->offset_y );
        fprintf( file, "%ff, %ff, ", glyph->advance_x, glyph->advance_y );
        fprintf( file, "%ff, %ff, %ff, %ff, ", glyph->s0, glyph->t0, glyph->s1, glyph->t1 );
        fprintf( file, "%" PRIzu ", ", vector_size(glyph->kerning) );
        if (vector_size(glyph->kerning) == 0) {
            fprintf( file, "0" );
        }
        else {
            fprintf( file, "{ " );
            for( j=0; j < vector_size(glyph->kerning); ++j )
            {
                kerning_t *kerning = (kerning_t *) vector_get( glyph->kerning, j);

                fprintf( file, "{%u, %ff}", kerning->codepoint, kerning->kerning );
                if( j < (vector_size(glyph->kerning)-1))
                {
                    fprintf( file, ", " );
                }
            }
            fprintf( file, "}" );
        }
        fprintf( file, " },\n" );
    }
    fprintf( file, " }\n};\n" );

    fprintf( file,
        "#ifdef __cplusplus\n"
        "}\n"
        "#endif\n" );

    fclose( file );

    return 0;
}
