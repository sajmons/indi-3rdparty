
#include "gphoto_readimage.h"

#include <indilogger.h>

#include <jpeglib.h>
#include <fitsio.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
// the older libraw uses auto_ptr
#include <libraw.h>
#pragma GCC diagnostic pop


#include <unistd.h>
#include <arpa/inet.h>


char dcraw_cmd[] = "dcraw";
char device[64];

#define err_printf IDLog,
struct dcraw_header
{
    time_t time;
    float exposure;
    int width;
    int height;
    int cfa_type;
    float wbr;
    float wbg;
    float wbgp;
    float wbb;
};

enum
{
    CFA_RGGB,
};

void gphoto_read_set_debug(const char *name)
{
    strncpy(device, name, 64);
}

void *tstrealloc(void *ptr, size_t size)
{
    DEBUGFDEVICE(device, INDI::Logger::DBG_DEBUG, "Realloc: %lu", (unsigned long)size);
    return realloc(ptr, size);
}

void addFITSKeywords(fitsfile *fptr)
{
    int status = 0;

    /* TODO add other data later */
    fits_write_date(fptr, &status);
}

int dcraw_parse_time(char *month, int day, int year, char *timestr)
{
    char mon_map[12][4] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    struct tm tm;
    int i;

    memset(&tm, 0, sizeof(struct tm));

    for (i = 0; i < 12; i++)
    {
        if (strncmp(month, mon_map[i], 3) == 0)
        {
            tm.tm_mon = i;
            break;
        }
    }
    tm.tm_year = year - 1900;
    tm.tm_mday = day;

    sscanf(timestr, "%d:%d:%d", &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
    return mktime(&tm);
}

int dcraw_parse_header_info(const char *filename, struct dcraw_header *header)
{
    FILE *handle;
    char line[256], cfa[80];
    char *cmd, timestr[10], month[10], daystr[10];
    int day, year;
    float r, g, b, gp;
    int ret = 0;

    memset(header, 0, sizeof(struct dcraw_header));
    ret = asprintf(&cmd, "%s -i -t 0 -v %s 2> /dev/null", dcraw_cmd, filename);
    DEBUGFDEVICE(device, INDI::Logger::DBG_DEBUG, "%s (%d)", cmd, ret);
    handle = popen(cmd, "r");
    free(cmd);
    if (handle == nullptr)
    {
        return 1;
    }

    while (fgets(line, sizeof(line), handle))
    {
        DEBUGFDEVICE(device, INDI::Logger::DBG_DEBUG, "%s", line);

        if (sscanf(line, "Timestamp: %s %s %d %s %d", daystr, month, &day, timestr, &year))
            header->time = dcraw_parse_time(month, day, year, timestr);
        else if (sscanf(line, "Shutter: 1/%f sec", &header->exposure))
            header->exposure = 1.0 / header->exposure;
        else if (sscanf(line, "Shutter: %f sec", &header->exposure))
            ;
        /*#ifdef USE_THUMB_SIZE
        else if (sscanf(line, "Thumb size: %d x %d", &header->width, &header->height) )
            ;
        #else*/
        else if (sscanf(line, "Output size: %d x %d", &header->width, &header->height))
            ;
        //#endif
        else if (sscanf(line, "Filter pattern: %s", cfa))
        {
            if (strncmp(cfa, "RGGBRGGBRGGBRGGB", sizeof(cfa)) == 0)
            {
                header->cfa_type = CFA_RGGB;
            }
        }
        else if (sscanf(line, "Camera multipliers: %f %f %f %f", &r, &g, &b, &gp) && r > 0.0)
        {
            header->wbr  = 1.0;
            header->wbg  = g / r;
            header->wbgp = gp / r;
            header->wbb  = b / r;
        }
    }

    pclose(handle);
    return 0;
}

int read_libraw(const char *filename, uint8_t **memptr, size_t *memsize, int *n_axis, int *w, int *h, int *bitsperpixel,
                char *bayer_pattern)
{
    int ret = 0;
    // Creation of image processing object
    LibRaw RawProcessor;

    // Let us open the file
    if ((ret = RawProcessor.open_file(filename)) != LIBRAW_SUCCESS)
    {
        DEBUGFDEVICE(device, INDI::Logger::DBG_ERROR, "Cannot open %s: %s", filename, libraw_strerror(ret));
        RawProcessor.recycle();
        return -1;
    }

    // Let us unpack the image
    if ((ret = RawProcessor.unpack()) != LIBRAW_SUCCESS)
    {
        DEBUGFDEVICE(device, INDI::Logger::DBG_ERROR, "Cannot unpack %s: %s", filename, libraw_strerror(ret));
        RawProcessor.recycle();
        return -1;
    }

    // Covert to image
    if ((ret = RawProcessor.raw2image()) != LIBRAW_SUCCESS)
    {
        DEBUGFDEVICE(device, INDI::Logger::DBG_ERROR, "Cannot convert %s : %s", filename, libraw_strerror(ret));
        RawProcessor.recycle();
        return -1;
    }

    *n_axis       = 2;
    *w            = RawProcessor.imgdata.rawdata.sizes.width;
    *h            = RawProcessor.imgdata.rawdata.sizes.height;
    *bitsperpixel = 16;
    // cdesc contains counter-clock wise e.g. RGBG CFA pattern while we want it sequential as RGGB
    bayer_pattern[0] = RawProcessor.imgdata.idata.cdesc[RawProcessor.COLOR(0, 0)];
    bayer_pattern[1] = RawProcessor.imgdata.idata.cdesc[RawProcessor.COLOR(0, 1)];
    bayer_pattern[2] = RawProcessor.imgdata.idata.cdesc[RawProcessor.COLOR(1, 0)];
    bayer_pattern[3] = RawProcessor.imgdata.idata.cdesc[RawProcessor.COLOR(1, 1)];
    bayer_pattern[4] = '\0';

    int first_visible_pixel = RawProcessor.imgdata.rawdata.sizes.raw_width * RawProcessor.imgdata.sizes.top_margin +
                              RawProcessor.imgdata.sizes.left_margin;

    DEBUGFDEVICE(device, INDI::Logger::DBG_DEBUG,
                 "read_libraw: raw_width: %d top_margin %d left_margin %d first_visible_pixel %d",
                 RawProcessor.imgdata.rawdata.sizes.raw_width, RawProcessor.imgdata.sizes.top_margin,
                 RawProcessor.imgdata.sizes.left_margin, first_visible_pixel);

    *memsize = RawProcessor.imgdata.rawdata.sizes.width * RawProcessor.imgdata.rawdata.sizes.height * sizeof(uint16_t);
    *memptr  = static_cast<uint8_t *>(IDSharedBlobRealloc(*memptr, *memsize));
    if (*memptr == nullptr)
        *memptr = static_cast<uint8_t *>(IDSharedBlobAlloc(*memsize));
    if (*memptr == nullptr)
    {
        DEBUGFDEVICE(device, INDI::Logger::DBG_ERROR, "%s: Failed to allocate %d bytes of memory!", __PRETTY_FUNCTION__, *memsize);
        return -1;
    }

    DEBUGFDEVICE(device, INDI::Logger::DBG_DEBUG,
                 "read_libraw: rawdata.sizes.width: %d rawdata.sizes.height %d memsize %d bayer_pattern %s",
                 RawProcessor.imgdata.rawdata.sizes.width, RawProcessor.imgdata.rawdata.sizes.height, *memsize,
                 bayer_pattern);

    uint16_t *image = reinterpret_cast<uint16_t *>(*memptr);
    uint16_t *src   = RawProcessor.imgdata.rawdata.raw_image + first_visible_pixel;

    for (int i = 0; i < RawProcessor.imgdata.rawdata.sizes.height; i++)
    {
        memcpy(image, src, RawProcessor.imgdata.rawdata.sizes.width * 2);
        image += RawProcessor.imgdata.rawdata.sizes.width;
        src += RawProcessor.imgdata.rawdata.sizes.raw_width;
    }

    return 0;
}

int read_jpeg(const char *filename, uint8_t **memptr, size_t *memsize, int *naxis, int *w, int *h)
{
    unsigned char *r_data = nullptr, *g_data = nullptr, *b_data = nullptr;

    /* these are standard libjpeg structures for reading(decompression) */
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    /* libjpeg data structure for storing one row, that is, scanline of an image */
    JSAMPROW row_pointer[1] = { nullptr };

    FILE *infile = fopen(filename, "rb");

    if (!infile)
    {
        DEBUGFDEVICE(device, INDI::Logger::DBG_DEBUG, "Error opening jpeg file %s!", filename);
        return -1;
    }
    /* here we set up the standard libjpeg error handler */
    cinfo.err = jpeg_std_error(&jerr);
    /* setup decompression process and source, then read JPEG header */
    jpeg_create_decompress(&cinfo);
    /* this makes the library read from infile */
    jpeg_stdio_src(&cinfo, infile);
    /* reading the image header which contains image information */
    jpeg_read_header(&cinfo, (boolean)TRUE);

    /* Start decompression jpeg here */
    jpeg_start_decompress(&cinfo);

    *memsize = cinfo.output_width * cinfo.output_height * cinfo.num_components;
    *memptr  = static_cast<uint8_t *>(IDSharedBlobRealloc(*memptr, *memsize));
    if (*memptr == nullptr)
        *memptr = static_cast<uint8_t *>(IDSharedBlobAlloc(*memsize));
    if (*memptr == nullptr)
    {
        DEBUGFDEVICE(device, INDI::Logger::DBG_ERROR, "%s: Failed to allocate %d bytes of memory!", __PRETTY_FUNCTION__, *memsize);
        return -1;
    }
    // if you do some ugly pointer math, remember to restore the original pointer or some random crashes will happen. This is why I do not like pointers!!
    uint8_t *oldmem = *memptr;
    *naxis = cinfo.num_components;
    *w     = cinfo.output_width;
    *h     = cinfo.output_height;

    /* now actually read the jpeg into the raw buffer */
    row_pointer[0] = (unsigned char *)malloc(cinfo.output_width * cinfo.num_components);
    if (cinfo.num_components)
    {
        r_data = (unsigned char *)*memptr;
        g_data = r_data + cinfo.output_width * cinfo.output_height;
        b_data = r_data + 2 * cinfo.output_width * cinfo.output_height;
    }
    /* read one scan line at a time */
    for (unsigned int row = 0; row < cinfo.image_height; row++)
    {
        unsigned char *ppm8 = row_pointer[0];
        jpeg_read_scanlines(&cinfo, row_pointer, 1);

        if (cinfo.num_components == 3)
        {
            for (unsigned int i = 0; i < cinfo.output_width; i++)
            {
                *r_data++ = *ppm8++;
                *g_data++ = *ppm8++;
                *b_data++ = *ppm8++;
            }
        }
        else
        {
            memcpy(*memptr, ppm8, cinfo.output_width);
            *memptr += cinfo.output_width;
        }
    }

    /* wrap up decompression, destroy objects, free pointers and close open files */
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    if (row_pointer[0])
        free(row_pointer[0]);
    if (infile)
        fclose(infile);

    *memptr = oldmem;

    return 0;
}

int read_jpeg_mem(unsigned char *inBuffer, unsigned long inSize, uint8_t **memptr, size_t *memsize, int *naxis, int *w,
                  int *h)
{
    /* these are standard libjpeg structures for reading(decompression) */
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    /* libjpeg data structure for storing one row, that is, scanline of an image */
    JSAMPROW row_pointer[1] = { nullptr };

    /* here we set up the standard libjpeg error handler */
    cinfo.err = jpeg_std_error(&jerr);
    /* setup decompression process and source, then read JPEG header */
    jpeg_create_decompress(&cinfo);
    /* this makes the library read from infile */
    jpeg_mem_src(&cinfo, inBuffer, inSize);

    /* reading the image header which contains image information */
    jpeg_read_header(&cinfo, (boolean)TRUE);

    /* Start decompression jpeg here */
    jpeg_start_decompress(&cinfo);

    *memsize = cinfo.output_width * cinfo.output_height * cinfo.num_components;
    *memptr  = static_cast<uint8_t *>(IDSharedBlobRealloc(*memptr, *memsize));
    if (*memptr == nullptr)
        *memptr = static_cast<uint8_t *>(IDSharedBlobAlloc(*memsize));
    if (*memptr == nullptr)
    {
        DEBUGFDEVICE(device, INDI::Logger::DBG_ERROR, "%s: Failed to allocate %d bytes of memory!", __PRETTY_FUNCTION__, *memsize);
        return -1;
    }

    uint8_t *destmem = *memptr;

    *naxis = cinfo.num_components;
    *w     = cinfo.output_width;
    *h     = cinfo.output_height;

    /* now actually read the jpeg into the raw buffer */
    row_pointer[0] = (unsigned char *)malloc(cinfo.output_width * cinfo.num_components);

    /* read one scan line at a time */
    for (unsigned int row = 0; row < cinfo.image_height; row++)
    {
        unsigned char *ppm8 = row_pointer[0];
        jpeg_read_scanlines(&cinfo, row_pointer, 1);
        memcpy(destmem, ppm8, cinfo.output_width * cinfo.num_components);
        destmem += cinfo.output_width * cinfo.num_components;
    }

    /* wrap up decompression, destroy objects, free pointers and close open files */
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    if (row_pointer[0])
        free(row_pointer[0]);

    return 0;
}

int read_jpeg_size(unsigned char *inBuffer, unsigned long inSize, int *w, int *h)
{
    /* these are standard libjpeg structures for reading(decompression) */
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;

    /* here we set up the standard libjpeg error handler */
    cinfo.err = jpeg_std_error(&jerr);
    /* setup decompression process and source, then read JPEG header */
    jpeg_create_decompress(&cinfo);
    /* this makes the library read from infile */
    jpeg_mem_src(&cinfo, inBuffer, inSize);

    /* reading the image header which contains image information */
    jpeg_read_header(&cinfo, (boolean)TRUE);

    *w     = cinfo.image_width;
    *h     = cinfo.image_height;

    /* wrap up decompression, destroy objects, free pointers and close open files */
    jpeg_destroy_decompress(&cinfo);

    return 0;
}
