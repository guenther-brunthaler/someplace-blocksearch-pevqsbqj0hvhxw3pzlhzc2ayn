#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

typedef struct resource_tag *resource;
struct resource_tag {
   resource older;
   void (*action)(void);
};

static resource rlist;

static void release_until(resource stop) {
   while (rlist) (*rlist->action)();
}

static void die(char const *emsg) {
   (void)fprintf(stderr, "%s\n", emsg);
   release_until(0);
   exit(EXIT_FAILURE);
}

static void *malloc_ck(size_t bytes) {
   void *memblk;
   if (memblk= malloc(bytes)) return memblk;
   die("Out of memory!");
}

static void push_resource(resource new_rsc, void (*new_action)()) {
   new_rsc->action= new_action;
   new_rsc->older= rlist;
   rlist= new_rsc;
}

static void *pop_resource(void) {
   resource r;
   assert(rlist);
   rlist= (r= rlist)->older;
   return r;
}

static void malloc_dtor(void) {
   free(pop_resource());
}

static void *malloc_resource(size_t total_bytes) {
   resource r;
   assert(total_bytes >= sizeof(*r));
   push_resource(r= malloc_ck(total_bytes), &malloc_dtor);
   return r;
}

typedef struct {
   char *start;
   size_t used;
   size_t allocated;
} buffer;

typedef struct {
   resource linkage;
   buffer descriptor;
} buffer_resource;

static void buffer_dtor(void) {
   buffer_resource *br= pop_resource();
   if (br->descriptor.allocated) {
      assert(br->descriptor.used <= br->descriptor.allocated);
      assert(br->descriptor.start);
      free(br->descriptor.start);
   } else {
      assert(!br->descriptor.start);
   }
   free(br);
}

static buffer *new_buffer(void) {
   buffer_resource *br= malloc_resource(sizeof *br);
   br->descriptor.start= 0;
   br->descriptor.allocated= 0;
   push_resource(br->linkage, buffer_dtor);
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
   die("Failed reallocating a memory block to a new size!");
}

static void grow_buffer(buffer *buf, size_t bigger_minimum_size) {
   size_t bytes;
   assert(bigger_minimum_size >= buf->used);
   assert(bigger_minimum_size > buf->allocated);
   for (bytes= 1; bytes < bigger_minimum_size; bytes+= bytes) {}
   buf->start= realloc_ck(buf->start, bytes);
   buf->allocated= bytes;
}

static void resize_buffer(buffer *buf, size_t exact_new_size) {
   assert(exact_new_size >= buf->used);
   buf->start= realloc_ck(buf->start, exact_new_size);
   buf->allocated= exact_new_size;
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

static buffer *load_remaining(FILE *fh) {
   buffer *buf;
   grow_buffer(buf= new_buffer(), 128);
   assert(!buf->used);
   for (;;) {
      size_t read;
      if (buf->used == buf->allocated) {
         grow_buffer(buf, buf->used + 1);
      }
      if (
         !(
            read= fread_ck(
                  buf->start + buf->used
               ,  sizeof(char)
               ,  buf->allocated - buf->used
               ,  fh
            )
         )
      ) {
         break;
      }
      buf->used+= read;
   }
   resize_buffer(buf, buf->used);
   return buf;
}

int main(int argc, char **argv) {
   char const *find;
   if (argc != 1) die("One argument: The file/device to be searched.");
   find= load_remaining(stdin)->start;
   release_until(0);
}
