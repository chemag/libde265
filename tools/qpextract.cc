/*
  libde265-based parser.

  MIT License

  Copyright (c) 2019 Facebook, Chema Gonzalez <chemag@gmail.com>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#define DO_MEMORY_LOGGING 0

#include "de265.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <getopt.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <limits>
#include <climits>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#include <signal.h>

#ifndef _MSC_VER
#include <sys/time.h>
#include <unistd.h>
#endif

#include "libde265/quality.h"

#define BUFFER_SIZE 40960

enum Procmode { qpymode, qpcbmode, qpcrmode, predmode };

bool nal_input = false;
bool check_hash = false;
bool show_help = false;
bool logging = true;
bool no_acceleration = false;
const char* bytestream_filename;
const char* reference_filename;
int highestTID = 100;
int maxQP = 52;
int minQP = 0;
Procmode procmode = qpymode;
bool weighted = false;
int verbosity = 0;
int disable_deblocking = 0;
int disable_sao = 0;
char* infile = NULL;
char* outfile = NULL;
FILE* fin = NULL;
FILE* fout = NULL;


// long options with no equivalent short option
enum {
  DISABLE_DEBLOCKING_OPTION = CHAR_MAX + 1,
  DISABLE_SAO_OPTION,
  QPYMODE_OPTION,
  QPCBMODE_OPTION,
  QPCRMODE_OPTION,
};


static struct option long_options[] = {
    {"check-hash", no_argument, nullptr, 'c'},
    {"weighted", no_argument, nullptr, 'w'},
    {"predmode", no_argument, nullptr, 'p'},
    {"frames", required_argument, nullptr, 'f'},
    {"infile", required_argument, nullptr, 'i'},
    {"outfile", required_argument, nullptr, 'o'},
    {"dump", no_argument, nullptr, 'd'},
    {"dump-image-data", no_argument, nullptr, 'I'},
    {"nal", no_argument, nullptr, 'n'},
    {"no-logging", no_argument, nullptr, 'L'},
    {"help", no_argument, nullptr, 'h'},
    {"noaccel", no_argument, nullptr, '0'},
    {"highest-TID", required_argument, nullptr, 'T'},
    {"verbose", no_argument, nullptr, 'v'},
    {"disable-deblocking", no_argument, &disable_deblocking, DISABLE_DEBLOCKING_OPTION},
    {"disable-sao", no_argument, &disable_sao, DISABLE_SAO_OPTION},
    {"qpymode", no_argument, nullptr, QPYMODE_OPTION},
    {"qpcbmode", no_argument, nullptr, QPCBMODE_OPTION},
    {"qpcrmode", no_argument, nullptr, QPCRMODE_OPTION},
    {"max-qp", required_argument, nullptr, 'Q'},
    {"min-qp", required_argument, nullptr, 'q'},
    {0, 0, 0, 0},
};

static int width, height;
static uint32_t framecnt = 0;

#ifdef HAVE___MALLOC_HOOK
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
static void* (*old_malloc_hook)(size_t, const void*);

static void* new_malloc_hook(size_t size, const void* caller) {
  void* mem;

  /*
  if (size>1000000) {
    raise(SIGINT);
  }
  */

  __malloc_hook = old_malloc_hook;
  mem = malloc(size);
  fprintf(stderr, "%p: malloc(%zu) = %p\n", caller, size, mem);
  __malloc_hook = new_malloc_hook;

  return mem;
}

static void init_my_hooks(void) {
  old_malloc_hook = __malloc_hook;
  __malloc_hook = new_malloc_hook;
}

#if DO_MEMORY_LOGGING
void (*volatile __malloc_initialize_hook)(void) = init_my_hooks;
#endif
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#endif

void dump_vps(video_parameter_set* vps) { vps->dump(STDOUT_FILENO); }

void dump_sps(seq_parameter_set* sps) { sps->dump(STDOUT_FILENO); }

void dump_pps(pic_parameter_set* pps) { pps->dump(STDOUT_FILENO); }

void get_qp_distro(const de265_image* img, int* qp_distro, bool weighted, Procmode procmode) {
  const seq_parameter_set& sps = img->get_sps();
  int minCbSize = sps.MinCbSizeY;

  // init QP distro
  for (int q = 0; q < 100; q++) qp_distro[q] = 0;

  // update QP distro
  for (int y0 = 0; y0 < sps.PicHeightInMinCbsY; y0++) {
    for (int x0 = 0; x0 < sps.PicWidthInMinCbsY; x0++) {
      int log2CbSize = img->get_log2CbSize_cbUnits(x0, y0);
      if (log2CbSize == 0) {
        continue;
      }

      int xb = x0 * minCbSize;
      int yb = y0 * minCbSize;

      int CbSize = 1 << log2CbSize;
      int qp = -1;
      if (procmode == qpymode) {
        qp = img->get_QPY(xb, yb);
      } else if (procmode == qpcbmode) {
        qp = img->get_QPCb(xb, yb);
      } else if (procmode == qpcrmode) {
        qp = img->get_QPCr(xb, yb);
      }
      if (qp < 0 || qp >= 100) {
        fprintf(stderr, "error: qp: %d\n", qp);
        continue;
      }
      // normalize the QP distro by CB size
      // qp_distro[qp] += (CbSize*CbSize);
      // provide per-block QP output
      qp_distro[qp] += (!weighted) ? 1 : (CbSize * CbSize);
    }
  }
  return;
}

void get_pred_distro(const de265_image* img, int* pred_distro, bool weighted) {
  const seq_parameter_set& sps = img->get_sps();
  int minCbSize = sps.MinCbSizeY;

  // init pred distro
  for (int pred_mode = 0; pred_mode < 100; pred_mode++) pred_distro[pred_mode] = 0;

  // update PredMode distro
  for (int y0 = 0; y0 < sps.PicHeightInMinCbsY; y0++) {
    for (int x0 = 0; x0 < sps.PicWidthInMinCbsY; x0++) {
      int log2CbSize = img->get_log2CbSize_cbUnits(x0, y0);
      if (log2CbSize == 0) {
        continue;
      }

      int xb = x0 * minCbSize;
      int yb = y0 * minCbSize;

      int CbSize = 1 << log2CbSize;
      enum PredMode pred_mode = img->get_pred_mode(xb, yb);
      if (pred_mode < 0 || pred_mode > 2) {
        fprintf(stderr, "error: pred_mode: %d\n", pred_mode);
        continue;
      }
      // normalize the PredMode distro by CB size
      // pred_distro[pred_mode] += (CbSize*CbSize);
      // provide per-block PredMode output
      pred_distro[pred_mode] += (!weighted) ? 1 : (CbSize * CbSize);
    }
  }
  return;
}

void dump_image_qp(de265_image* img, Procmode procmode) {
#define BUFSIZE 1024
  char buffer[BUFSIZE] = {};
  int bi = 0;

  // calculate QP distro
  int qp_distro[100];
  get_qp_distro(img, qp_distro, weighted, procmode);

  // dump frame number
  bi += snprintf(buffer + bi, BUFSIZE - bi, "%i,", img->get_ID());

  // dump QP statistics
  int qp_max = -1;
  int qp_min = -1;
  int qp_sum = 0;
  int qp_num = 0;
  for (int qp = minQP; qp <= maxQP; qp++) {
    if ((qp_distro[qp] > 0) && (qp_max == -1 || qp > qp_max)) {
        qp_max = qp;
    }
    if ((qp_distro[qp] > 0) && (qp_min == -1 || qp < qp_min)) {
        qp_min = qp;
    }
    qp_sum += qp * qp_distro[qp];
    qp_num += qp_distro[qp];
  }
  double qp_avg = (double)qp_sum / qp_num;
  double qp_sumsquare = 0;
  for (int qp = minQP; qp <= maxQP; qp++) {
    double diff = qp - qp_avg;
    qp_sumsquare += (diff) * (diff) * qp_distro[qp];
  }
  double qp_stddev = sqrt(qp_sumsquare / qp_num);
  bi += snprintf(buffer + bi, BUFSIZE - bi, "%i,%i,%i,%f,%f,", qp_num, qp_min, qp_max, qp_avg, qp_stddev);

  // dump QP distro
  for (int qp = minQP; qp <= maxQP; qp++) {
    bi += snprintf(buffer + bi, BUFSIZE - bi, "%i,", qp_distro[qp]);
  }
  buffer[bi - 1] = '\n';
  fprintf(fout, buffer);

  if (!verbosity) {
    return;
  }

  // dump all the QP values
  const seq_parameter_set& sps = img->get_sps();
  int minCbSize = sps.MinCbSizeY;

  for (int y0 = 0; y0 < sps.PicHeightInMinCbsY; y0++) {
    for (int x0 = 0; x0 < sps.PicWidthInMinCbsY; x0++) {
      int log2CbSize = img->get_log2CbSize_cbUnits(x0, y0);
      if (log2CbSize == 0) {
        continue;
      }

      int xb = x0 * minCbSize;
      int yb = y0 * minCbSize;

      int CbSize = 1 << log2CbSize;

      int q = img->get_QPY(xb, yb);
      if (q < 0 || q >= 100) {
        fprintf(stderr, "error: q: %d\n", q);
        continue;
      }
      // provide per-block QP output
      fprintf(stderr, "id: %i qp_coord[%i,%i]: %i CbSize: %i\n", img->get_ID(),
              xb, yb, q, CbSize);
    }
  }

  fprintf(fout, buffer);
}

void dump_image_pred(de265_image* img) {
#define BUFSIZE 1024
  char buffer[BUFSIZE] = {};
  int bi = 0;

  // calculate pred distro
  int pred_distro[3] = { 0 };
  get_pred_distro(img, pred_distro, weighted);

  // dump frame number
  bi += snprintf(buffer + bi, BUFSIZE - bi, "%i,", img->get_ID());

  // dump PredMode distro
  int sum = 0;
  for (int pred_mode = 0; pred_mode < 3; pred_mode++) {
    bi += snprintf(buffer + bi, BUFSIZE - bi, "%i,", pred_distro[pred_mode]);
    sum += pred_distro[pred_mode];
  }

  // dump PredMode ratio
  for (int pred_mode = 0; pred_mode < 3; pred_mode++) {
    double ratio = (double)pred_distro[pred_mode] / sum;
    bi += snprintf(buffer + bi, BUFSIZE - bi, "%f,", ratio);
  }

  buffer[bi - 1] = '\n';
  fprintf(fout, buffer);
}

void dump_image(de265_image* img) {
  if ((procmode == qpymode) || (procmode == qpcbmode) || (procmode == qpcrmode)) {
    dump_image_qp(img, procmode);
  } else if (procmode == predmode) {
    dump_image_pred(img);
  }
}

void usage(char* argv0) {
  fprintf(stderr, "# qpextract  v%s\n", de265_get_version());
  fprintf(stderr, "usage: %s [options] -i videofile.bin [-o output.csv]\n",
          argv0);
  fprintf(stderr,
          "The video file must be a raw bitstream, or a stream with NAL "
          "units (option -n).\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "options:\n");
  fprintf(stderr, "  -c, --check-hash  perform hash check\n");
  fprintf(stderr,
          "  -n, --nal         input is a stream with 4-byte length prefixed "
          "NAL units\n");
  fprintf(stderr, "  -d, --dump        dump headers\n");
  fprintf(stderr,
          "  -T, --highest-TID select highest temporal sublayer to decode\n");
  fprintf(stderr, "      --disable-deblocking   disable deblocking filter\n");
  fprintf(
      stderr,
      "      --disable-sao          disable sample-adaptive offset filter\n");
  fprintf(stderr, "  -q, --min-qp      minimum QP for CSV dump\n");
  fprintf(stderr, "  -Q, --max-qp      maximum QP for CSV dump\n");
  fprintf(stderr, "  -w, --weighted    weighted mode (multiply each QP times the number of pixels)\n");
  fprintf(stderr, "  --qpymode         QPY mode (get the distribution of QP Y values)\n");
  fprintf(stderr, "  --qpcbmode        QPCb mode (get the distribution of QP Cb values)\n");
  fprintf(stderr, "  --qpcrmode        QPCr mode (get the distribution of QP Cr values)\n");
  fprintf(stderr, "  -p, --predmode    pred mode (get the distribution of prediction modes)\n");
  fprintf(stderr, "  -h, --help        show help\n");
}

int main(int argc, char** argv) {
  char* endptr;

  while (1) {
    int option_index = 0;

    int c = getopt_long(argc, argv, "t:chfpq:Q:i:o:dILB:n0vT:m:sew",
                        long_options, &option_index);
    if (c == -1) break;

    switch (c) {
      case 0:
        // long options that define flag
        // if this option set a flag, do nothing else now
        if (long_options[optind].flag != nullptr) {
          break;
        }
        printf("option %s", long_options[optind].name);
        if (optarg) {
          printf(" with arg %s", optarg);
        }
        break;
      case 'c':
        check_hash = true;
        break;
      case 'h':
        show_help = true;
        break;
      case 'n':
        nal_input = true;
        break;
      case 'L':
        logging = false;
        break;
      case 'p':
        procmode = predmode;
        break;
      case '0':
        no_acceleration = true;
        break;
      case 'T':
        highestTID = strtol(optarg, &endptr, 0);
        if (*endptr != '\0') {
          usage(argv[0]);
          exit(-1);
        }
        break;
      case 'Q':
        maxQP = strtol(optarg, &endptr, 0);
        if (*endptr != '\0') {
          usage(argv[0]);
          exit(-1);
        }
        break;
      case 'q':
        minQP = strtol(optarg, &endptr, 0);
        if (*endptr != '\0') {
          usage(argv[0]);
          exit(-1);
        }
        break;
      case 'v':
        verbosity++;
        break;
      case 'w':
        weighted = true;
        break;
      case 'i':
        infile = optarg;
        break;
      case 'o':
        outfile = optarg;
        break;
      case DISABLE_DEBLOCKING_OPTION:
        disable_deblocking = 1;
        break;
      case DISABLE_SAO_OPTION:
        disable_sao = 1;
        break;
      case QPYMODE_OPTION:
        procmode = qpymode;
        break;
      case QPCBMODE_OPTION:
        procmode = qpcbmode;
        break;
      case QPCRMODE_OPTION:
        procmode = qpcrmode;
        break;
    }
  }

  if (show_help) {
    usage(argv[0]);
    exit(show_help ? 0 : 5);
  }

  // create and configure decoder
  de265_error err = DE265_OK;
  de265_decoder_context* ctx = de265_new_decoder();
  de265_set_parameter_bool(ctx, DE265_DECODER_PARAM_BOOL_SEI_CHECK_HASH,
                           check_hash);
  de265_set_parameter_bool(ctx, DE265_DECODER_PARAM_SUPPRESS_FAULTY_PICTURES,
                           false);
  de265_set_parameter_bool(ctx, DE265_DECODER_PARAM_DISABLE_DEBLOCKING,
                           disable_deblocking);
  de265_set_parameter_bool(ctx, DE265_DECODER_PARAM_DISABLE_SAO, disable_sao);

  // if (dump_headers) {
  //   de265_set_parameter_int(ctx, DE265_DECODER_PARAM_DUMP_SEI, 1);
  //   de265_set_parameter_int(ctx, DE265_DECODER_PARAM_DUMP_SLICE_HEADERS, 1);
  // }

  if (no_acceleration) {
    de265_set_parameter_int(ctx, DE265_DECODER_PARAM_ACCELERATION_CODE,
                            de265_acceleration_SCALAR);
  }

  if (!logging) {
    de265_disable_logging();
  }

  de265_set_verbosity(verbosity);

  de265_set_limit_TID(ctx, highestTID);

  // set callback
  struct de265_callback_block cb;
#if 0
  cb.get_vps = dump_vps;
  cb.get_sps = dump_sps;
  cb.get_pps = dump_pps;
#else
  cb.get_vps = NULL;
  cb.get_sps = NULL;
  cb.get_pps = NULL;
#endif
  cb.get_image = dump_image;
  de265_callback_register(ctx, &cb);

  // get a valid input file pointer
  if (infile == NULL or strncmp("-", infile, 1) == 0) {
    fin = stdin;
  } else {
    // open the input file
    fin = fopen(infile, "rb");
    if (fin == NULL) {
      fprintf(stderr, "cannot open file %s!\n", infile);
      exit(10);
    }
  }

  // get a valid output file pointer
  if (outfile == NULL or strncmp("-", outfile, 1) == 0) {
    fout = stdout;
  } else {
    // open the output file
    fout = fopen(outfile, "wb");
    if (fout == NULL) {
      fprintf(stderr, "cannot open file %s!\n", outfile);
      exit(10);
    }
  }

  // dump the CSV header
#define BUFSIZE 1024
  char buffer[BUFSIZE] = {};
  int bi = 0;
  if ((procmode == qpymode) || (procmode == qpcbmode) || (procmode == qpcrmode)) {
      bi += snprintf(buffer + bi, BUFSIZE - bi, "frame,qp_num,qp_min,qp_max,qp_avg,qp_stddev");
      for (int qp = minQP; qp <= maxQP; qp++) {
        bi += snprintf(buffer + bi, BUFSIZE - bi, ",%i", qp);
      }
  } else if (procmode == predmode) {
      bi += snprintf(buffer + bi, BUFSIZE - bi, "frame,intra,inter,skip,intra_ratio,inter_ratio,skip_ratio");
  }
  buffer[bi] = '\n';
  fprintf(fout, buffer);

  bool stop = false;

  int pos = 0;

  while (!stop) {
    if (nal_input) {
      uint8_t len[4];
      int n = fread(len, 1, 4, fin);
      int length = (len[0] << 24) + (len[1] << 16) + (len[2] << 8) + len[3];

      uint8_t* buf = (uint8_t*)malloc(length);
      n = fread(buf, 1, length, fin);
      err = de265_push_NAL(ctx, buf, n, pos, (void*)1);
      free(buf);
      pos += n;
    } else {
      // read a chunk of input data
      uint8_t buf[BUFFER_SIZE];
      int n = fread(buf, 1, BUFFER_SIZE, fin);

      // decode input data
      if (n) {
        err = de265_push_data(ctx, buf, n, pos, (void*)2);
        if (err != DE265_OK) {
          break;
        }
      }

      pos += n;
    }

    if (feof(fin)) {
      err = de265_flush_data(ctx);  // indicate end of stream
      stop = true;
    }

    // decoding loop
    int more = 1;
    while (more) {
      more = 0;

      // decode some more
      err = de265_decode(ctx, &more);
      if (err != DE265_OK) {
        if (check_hash && err == DE265_ERROR_CHECKSUM_MISMATCH) stop = 1;
        more = 0;
        break;
      }

      // show available images
      const de265_image* img = de265_get_next_picture(ctx);

      // show warnings
      for (;;) {
        de265_error warning = de265_get_warning(ctx);
        if (warning == DE265_OK) {
          break;
        }
      }
    }
  }

  // clean up file pointers
  fclose(fin);
  fclose(fout);

  de265_free_decoder(ctx);

  return err == DE265_OK ? 0 : 10;
}
