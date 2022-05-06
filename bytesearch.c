static char VERSION_INFO[]= {
   "Version 2022.125\n"
   "Copyright (c) 2022 Guenther Brunthaler. All rights reserved.\n"
   "\n"
   "This program is free software.\n"
   "Distribution is permitted under the terms of the GPLv3.\n"
};

static char HELP[]= {
   "\n"
   "Usage: %s [ <options> ... [--] ]\n"
   "       <buffer_size> <haystack> [ <start> ] < <needle>\n"
   "\n"
   "Read contents of file <haystack> starting at offset <start>\n"
   "(defaults to 0) in chunks of fixed size <buffer_size> except for\n"
   "the last chunk which may be smaller. <haystack> may also be a\n"
   "special file like a block device.\n"
   "\n"
   "Search the contents of every chunk for a byte sequence <needle>\n"
   "read from standard input.\n"
   "\n"
   "Output the byte offset into <haystack> of the first match found.\n"
   "If no match is found, output an empty line insted.\n"
   "\n"
   "<buffer_size>, <start> and the returned match offset are all\n"
   "hexadecimal values without any radix prefix. All units of\n"
   "measurements are bytes (neither sectors, blocks, kB, MB nor\n"
   "anything else).\n"
   "\n"
   "%s"
};

#define _POSIX_C_SOURCE 1
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>

typedef struct resource resource;
struct resource {
   resource *older;
   void (*action)(void);
};

static resource *rlist;
static int errors;

static void release_tracked_until(resource *stop) {
   while (rlist) (*rlist->action)();
}

static void die(char const *emsg) {
   if (!errors) errors= -1; /* Means 1 or more errors. */
   (void)fprintf(stderr, "%s\n", emsg);
   release_tracked_until(0);
   exit(EXIT_FAILURE);
}

static void *malloc_ck(size_t bytes) {
   void *memblk;
   if (memblk= malloc(bytes)) return memblk;
   die("Out of memory!");
}

static void track_resource(resource *new_rsc, void (*new_action)()) {
   new_rsc->action= new_action;
   new_rsc->older= rlist;
   rlist= new_rsc;
}

static void *untrack_resource(void) {
   resource *r;
   assert(rlist);
   rlist= (r= rlist)->older;
   return r;
}

static void malloc_dtor(void) {
   free(untrack_resource());
}

static void *new_tracked_resource(size_t total_bytes) {
   resource *r;
   assert(total_bytes >= sizeof(*r));
   track_resource(r= malloc_ck(total_bytes), &malloc_dtor);
   return r;
}

typedef struct {
   char *start; /* If NULL then yet unallocated buffer. */
   size_t length; /* Must be <= capacity but only if capacity != 0. */
   size_t capacity; /* If non-zero or start == NULL: Slice is resizable. */
} slice;

typedef struct {
   resource rsrc;
   slice descriptor;
} buffer_resource;

static void buffer_dtor(void) {
   buffer_resource *br= untrack_resource();
   if (br->descriptor.capacity) {
      assert(br->descriptor.length <= br->descriptor.capacity);
      assert(br->descriptor.start);
      free(br->descriptor.start);
   } else {
      assert(!br->descriptor.start);
   }
   free(br);
}

static slice *new_tracked_buffer(void) {
   buffer_resource *br= new_tracked_resource(sizeof *br);
   br->descriptor.start= 0;
   br->descriptor.capacity= br->descriptor.length= 0;
   br->rsrc.action= buffer_dtor;
   return &br->descriptor;
}

static void *realloc_ck(void *p, size_t newsz) {
   void *pnew;
   if (!newsz) {
      assert(p);
      free(p);
      return 0;
   }
   if (!p) return malloc_ck(newsz);
   if (pnew= realloc(p, newsz)) return pnew;
   die("Could not resize the memory allocation!");
}

static void grow_buffer(slice *buf, size_t bigger_minimum_size) {
   size_t bytes;
   assert(bigger_minimum_size >= buf->length);
   assert(bigger_minimum_size > buf->capacity);
   for (bytes= 1; bytes < bigger_minimum_size; bytes+= bytes) {}
   buf->start= realloc_ck(buf->start, bytes);
   buf->capacity= bytes;
}

static void grow_buffer_by(slice *buf, size_t increment) {
   assert(increment > 0);
   grow_buffer(buf, buf->length + increment);
}

static void resize_buffer(slice *buf, size_t exact_new_size) {
   assert(exact_new_size >= buf->length);
   buf->start= realloc_ck(buf->start, exact_new_size);
   buf->capacity= exact_new_size;
}

static size_t fread_ck(void *dst, size_t elem_size, size_t n_elem, FILE *fh) {
   size_t read;
   assert(dst); assert(elem_size); assert(n_elem); assert(fh);
   if ((read= fread(dst, elem_size, n_elem, fh)) != n_elem) {
      if (ferror(fh)) die("Read error!");
      assert(feof(fh));
   }
   return read;
}

static slice *tracked_stream_remainder(FILE *fh) {
   slice *buf;
   grow_buffer(buf= new_tracked_buffer(), 128);
   assert(!buf->length);
   for (;;) {
      size_t read;
      if (buf->length == buf->capacity) grow_buffer_by(buf, 1);
      if (
         !(
            read= fread_ck(
                  buf->start + buf->length
               ,  sizeof(char)
               ,  buf->capacity - buf->length
               ,  fh
            )
         )
      ) {
         break;
      }
      buf->length+= read;
   }
   resize_buffer(buf, buf->length);
   return buf;
}

static void write_error(void) {
   die("Write error!");
}

static void fflush_ck(FILE *stream) {
   if (fflush(stream)) write_error();
}

static void freopen_ck(const char *pathname, const char *mode, FILE *stream) {
   FILE *result;
   if ((result= freopen(pathname, mode, stream)) != stream) {
      assert(!result);
      die("Could not open stream!");
   }
}

static void putchar_ck(int chr) {
   if (putchar(chr) != chr) write_error();
}

static char xdigits[]= "0123456789abcdef";

static void print_size_t(size_t value) {
   if (value >= 0x10) {
      print_size_t(value >> 4);
      value&= 0xf;
   }
   putchar_ck(xdigits[value]);
}

static off_t convert_off_t(char const *hex) {
   static char map[]= "AaBbCcDdEeFf";
   off_t value= 0;
   if (!*hex) die("Number without any digits!");
   do {
      off_t nvalue;
      char *found;
      {
         int digit;
         if (found= strchr(map, (digit= *hex))) {
            int off;
            if ((off= (int)(found - map) & 1) == 0) {
               digit= map[off + 1];
            }
         }
         if (!(found= strchr(xdigits, digit))) {
            die("Invalid hexadecimal digit in number!");
         }
      }
      if ((nvalue= value << 4 | (off_t)(found - xdigits)) < value) {
         die("Hexadecimal number exceeds its supported maximum value!");
      }
      value= nvalue;
   } while (*++hex);
   return value;
}

static int scan4match(
   FILE *haystack, slice const *needle, slice *work, size_t fpos
) {
   if (needle->length > work->capacity) {
      die("Buffer needs to be at least as large as <needle>!");
   }
   {
      size_t read;
      while (
         read= fread_ck(
            work->start, sizeof(char), work->capacity, haystack
         )
      ) {
         size_t already_matched, boff;
         already_matched= boff= 0;
         while (boff != read) {
            if (needle->start[already_matched] == work->start[boff]) {
               if (++already_matched == needle->length) {
                  print_size_t(fpos + boff - already_matched + 1);
                  goto done;
               }
            } else if (already_matched) {
               boff-= already_matched;
               already_matched= 0;
            }
            ++boff;
         }
         fpos+= read;
      }
   }
   done:
   putchar_ck('\n');
}

static off_t lseek_ck(int fildes, off_t offset, int whence) {
   off_t new;
   errno= 0;
   if ((new= lseek(fildes, offset, whence)) != -1 || !errno) return new;
   die("Failure changing the current file offset position!");
}

typedef struct {
   resource rsrc;
   char const *prog_name;
} usage_resource;

static void usage_action(void) {
   usage_resource *r= untrack_resource();
   if (errors) (void)fprintf(stderr, HELP, r->prog_name, VERSION_INFO);
}

static void flusher_action() {
   (void)untrack_resource();
   fflush_ck(0);
}

int main(int argc, char **argv) {
   usage_resource usage;
   resource flusher;
   slice *needle, *work;
   off_t start= 0;
   track_resource(&flusher, &flusher_action);
   {
      int arg= 0;
      usage.prog_name= "(unnamed_program)";
      track_resource(&usage.rsrc, &usage_action);
      if (arg == argc) {
         more_args:
         die("Too few arguments!");
      }
      usage.prog_name= argv[arg];
      if (++arg == argc) goto more_args;
      if (!strcmp(argv[arg], "--")) {
         if (++arg == argc) goto more_args;
      }
      if (*argv[arg] == '-') die("Unknown option!");
      resize_buffer(work= new_tracked_buffer(), convert_off_t(argv[arg]));
      if (++arg == argc) goto more_args;
      needle= tracked_stream_remainder(stdin);
      freopen_ck(argv[arg], "r", stdin);
      if (++arg == argc) goto args_done;
      lseek_ck(fileno(stdin), start= convert_off_t(argv[arg]), SEEK_SET);
      if (++arg == argc) goto args_done;
      die("Too many arguments!");
   }
   args_done:
   scan4match(stdin, needle, work, start);
   release_tracked_until(0);
}
