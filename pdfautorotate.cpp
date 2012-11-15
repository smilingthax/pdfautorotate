// Copyright (c) 2012 Tobias Hoffmann
// MIT Licensed.

// note that  fit-plot  is already well-supported

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
//#include <qpdf/QUtil.hh>

#include <assert.h>
#include <cups/cups.h>
#include <cups/ppd.h>

#include <stdarg.h>
static void error(const char *fmt,...) // {{{
{
  va_list ap;
  va_start(ap,fmt);

  fputs("Error: ",stderr);
  vfprintf(stderr,fmt,ap);
  fputs("\n",stderr);

  va_end(ap);
}
// }}}

QPDFObjectHandle makeBox(double x1,double y1,double x2,double y2) // {{{
{
  QPDFObjectHandle ret=QPDFObjectHandle::newArray();
  ret.appendItem(QPDFObjectHandle::newReal(x1));
  ret.appendItem(QPDFObjectHandle::newReal(y1));
  ret.appendItem(QPDFObjectHandle::newReal(x2));
  ret.appendItem(QPDFObjectHandle::newReal(y2));
  return ret;
}
// }}}

QPDFObjectHandle getTrimBox(QPDFObjectHandle &page) // {{{
{
  if (page.hasKey("/TrimBox")) {
    return page.getKey("/TrimBox");
  } else if (page.hasKey("/CropBox")) {
    return page.getKey("/CropBox");
  }
  return page.getKey("/MediaBox");
}
// }}}

enum Rotation { ROT_0, ROT_90, ROT_180, ROT_270 };  // CCW

Rotation operator+(Rotation lhs,Rotation rhs) // {{{
{
  return (Rotation)(((int)lhs+(int)rhs)%4);
}
// }}}

Rotation getRotate(QPDFObjectHandle &page) // {{{
{
  if (!page.hasKey("/Rotate")) {
    return ROT_0;
  }
  double rot=page.getKey("/Rotate").getNumericValue();
  if (rot==90.0) { // CW 
    return ROT_270; // CCW
  } else if (rot==180.0) {
    return ROT_180;
  } else if (rot==270.0) {
    return ROT_90;
  } else {
    assert(rot==0.0);
  }
  return ROT_0;
}
// }}}

QPDFObjectHandle makeRotate(Rotation rot) // {{{
{
  switch (rot) {
  case ROT_0:
    return QPDFObjectHandle::newNull();
  case ROT_90: // CCW
    return QPDFObjectHandle::newInteger(270); // CW
  case ROT_180:
    return QPDFObjectHandle::newInteger(180);
  case ROT_270:
    return QPDFObjectHandle::newInteger(90);
  default:
    assert(0);
  }
}
// }}}

struct Params {
  Params()
    : normal_landscape(ROT_270),
      orientation(ROT_0),
      paper_is_landscape(false),
      autoRotate(true)
  {}

  Rotation normal_landscape,orientation;
  bool paper_is_landscape;
  bool autoRotate;
};

static bool is_false(const char *value) // {{{
{
  if (!value) {
    return false;
  }
  return (strcasecmp(value,"no")==0)||
         (strcasecmp(value,"off")==0)||
         (strcasecmp(value,"false")==0);
}
// }}}

static bool optGetInt(const char *name,int num_options,cups_option_t *options,int *ret) // {{{
{
  assert(ret);
  const char *val=cupsGetOption(name,num_options,options);
  if (val) {
    *ret=atoi(val);
    return true;
  }
  return false;
}
// }}}

// FIXME? normal_landscape not derived from PPD
// parse options, look for "pdfAutoRotate" key, also check landscape/orientation_requested
void processOptions(ppd_file_t *ppd,int num_options,cups_option_t *options,Params &param) // {{{
{
  const char *val;

  if ( (ppd)&&(ppd->landscape>0) ) { // direction the printer rotates landscape (90 or -90)
    param.normal_landscape=ROT_90;
  } else {
    param.normal_landscape=ROT_270;
  }

  ppd_size_t *pagesize;
  param.paper_is_landscape=false;
  if ( (pagesize=ppdPageSize(ppd,0)) != NULL) { // "already rotated"
    if (pagesize->width>pagesize->length) {
      param.paper_is_landscape=true;
    }
  }

  int ipprot;
  param.orientation=ROT_0;
  if ( (val=cupsGetOption("landscape",num_options,options)) != NULL) {
    if (!is_false(val)) {
      param.orientation=param.normal_landscape;
    }
  } else if (optGetInt("orientation-requested",num_options,options,&ipprot)) {
    /* IPP orientation values are:
     *   3: 0 degrees,  4: 90 degrees,  5: -90 degrees,  6: 180 degrees
     */
    if ( (ipprot<3)||(ipprot>6) ) {
      error("Bad value (%d) for orientation-requested, using 0 degrees",ipprot);
    } else {
      static const Rotation ipp2rot[4]={ROT_0, ROT_90, ROT_270, ROT_180};
      param.orientation=ipp2rot[ipprot-3];
    }
  }

  if ( (val=cupsGetOption("pdfAutoRotate",num_options,options)) != NULL) {
    param.autoRotate=!is_false(val);
  }
}
// }}}

static bool isLandscape(Rotation orientation) // {{{
{
  return (orientation==ROT_90)||(orientation==ROT_270);
}
// }}}

void processPDF(QPDF &pdf,Params &param) // {{{
{
  const std::vector<QPDFObjectHandle> &pages=pdf.getAllPages();
  const int len=pages.size();

  const bool dst_lscape=( param.paper_is_landscape!=isLandscape(param.orientation) );

  for (int iA=0;iA<len;iA++) {
    QPDFObjectHandle page=pages[iA];

    if (param.autoRotate) {
      QPDFObjectHandle rect=getTrimBox(page);
      const double width=rect.getArrayItem(2).getNumericValue()-rect.getArrayItem(0).getNumericValue(),
                   height=rect.getArrayItem(3).getNumericValue()-rect.getArrayItem(1).getNumericValue();

      Rotation src_rot=getRotate(page);
      const bool src_lscape=( (width<height)==isLandscape(src_rot) );

      if (src_lscape!=dst_lscape) {
        Rotation rotation=param.normal_landscape; // ROT_90;
        // TODO? other rotation direction, e.g. if (src_rot==ROT_0)&&(param.orientation==ROT_270) ... etc.
        // rotation=ROT_270;

        page.replaceOrRemoveKey("/Rotate",makeRotate(src_rot+rotation));
      }
    }
  }
}
// }}}

// reads from stdin into temporary file. returns FILE *  or NULL on error 
// TODO? to extra file (also used in pdftoijs, e.g.)
FILE *copy_stdin_to_temp() // {{{
{
  char buf[BUFSIZ];
  int n;

  // FIXME:  what does >buf mean here?
  int fd=cupsTempFd(buf,sizeof(buf));
  if (fd<0) {
    error("Can't create temporary file");
    return NULL;
  }
  // remove name
  unlink(buf);

  // copy stdin to the tmp file
  while ( (n=read(0,buf,BUFSIZ)) > 0) {
    if (write(fd,buf,n) != n) {
      error("Can't copy stdin to temporary file");
      close(fd);
      return NULL;
    }
  }
  if (lseek(fd,0,SEEK_SET) < 0) {
    error("Can't rewind temporary file");
    close(fd);
    return NULL;
  }

  FILE *f;
  if ( (f=fdopen(fd,"rb")) == 0) {
    error("Can't fdopen temporary file");
    close(fd);
    return NULL;
  }
  return f;
}
// }}}

int main(int argc,char **argv)
{
  try {
    if ( (argc<6)||(argc>7) ) {
      fprintf(stderr,"Usage: %s job-id user title copies options [file]\n",argv[0]);
      return 1;
    }

    QPDF pdf;
    Params param;
    
    if (argc==6) {
      try {
        FILE *input=copy_stdin_to_temp();
        if (!input) {
          return 2;
        }
        pdf.processFile("temp file",input,true);
      } catch (const std::exception &e) {
        error("loadFile failed: %s",e.what());
        return 2;
      }
    } else {
      try {
        pdf.processFile(argv[6]);
      } catch (const std::exception &e) {
        error("loadFile failed: %s",e.what());
        return 2;
      }
    }

    // parse options
    int num_options=0;
    cups_option_t *options=NULL;
    num_options=cupsParseOptions(argv[5],num_options,&options);

    // don't forget the ppd
    ppd_file_t *ppd=NULL;
    ppd=ppdOpenFile(getenv("PPD")); // getenv (and thus ppd) may be null. This will not cause problems.
    ppdMarkDefaults(ppd);
    cupsMarkOptions(ppd,num_options,options);

    processOptions(ppd,num_options,options,param);

    cupsFreeOptions(num_options,options);

    // apply processing
    processPDF(pdf,param);

    // write 
    QPDFWriter output(pdf,NULL);
    output.write();
  } catch (std::exception &ex) {
    error("Ex: %s\n",ex.what());
    return 5;
  } catch (...) {
    error("Unknown exception caught. Exiting.\n");
    return 6;
  }

  return 0;
}
