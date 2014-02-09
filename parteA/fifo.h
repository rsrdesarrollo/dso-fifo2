#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/semaphore.h>
#include "cbuffer.h"

MODULE_AUTHOR("Raúl Sampedro Ruiz");
MODULE_DESCRIPTION("Multiples fifos");
MODULE_LICENSE("GPL");

#define DEVICE_NAME "fifodev"
#define BUF_LEN 512
#define FIFO_DEBUG
//#define DEBUG_VERBOSE

#ifdef FIFO_DEBUG
    #define DBG(format, arg...) do { \
        printk(KERN_DEBUG "%s: " format "\n" , __func__ , ## arg); \
    } while (0)

    #ifdef DEBUG_VERBOSE
        #define DBGV DBG
    #else
        #define DBGV(format, args...) /* */
    #endif
#else
    #define DBG(format, arg...) /* */
    #define DBGV(format, args...) /* */
#endif

int init_module(void);
void cleanup_module(void);
static int fifo_open(struct inode *, struct file *);
static int fifo_release(struct inode *, struct file *);
static ssize_t fifo_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t fifo_write(struct file * ,const char __user * ,size_t ,loff_t *);

struct fifo_t {
    cbuffer_t* buff;
    struct semaphore cola_prod;
    struct semaphore cola_cons;
    struct semaphore mutex;

    int num_cons;
    int num_prod;
        
    int num_bloq_prod;
    int num_bloq_cons;
};

typedef struct {
    struct list_head links;
    struct fifo_t fifo;
}fifo_list_t;

static int init_fifo(struct fifo_t* f){
    f->buff = create_cbuffer_t(BUF_LEN);
    sema_init(&f->cola_prod, 0);
    sema_init(&f->cola_cons, 0);
    sema_init(&f->mutex, 1);

    f->num_cons = 0;
    f->num_prod = 0;

    f->num_bloq_prod = 0;
    f->num_bloq_cons = 0;

    return (f->buff == NULL)? -ENOMEM : 0;
}

static void clear_fifo(struct fifo_t* f){
    f->buff->size = 0;
}

static void destroy_fifo(struct fifo_t* f){
    destroy_cbuffer_t(f->buff);
}


#define cond_wait(mtx, cond, count, interrupt_InterruptHandler) \
    do { \
        count++; \
        up(mtx); \
        DBGV("Me voy a dormir en "#cond" con "#count": %d", count); \
        if (down_interruptible(cond)){ \
            DBGV("[INT] Despertado de "#cond" por interrupción }:(");\
            down(mtx); \
            count--; \
            up(mtx); \
            interrupt_InterruptHandler \
            return -EINTR; \
        } \
        DBGV("Me han despertado de "#cond);\
        if (down_interruptible(mtx)){ \
            interrupt_InterruptHandler \
            return -EINTR; \
        }\
    } while(0)

#define cond_signal(cond) up(cond)

#define __InterruptHandler__

