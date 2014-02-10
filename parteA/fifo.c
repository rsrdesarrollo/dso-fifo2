#include <linux/vmalloc.h>
#include <asm-generic/uaccess.h>
#include <asm-generic/errno.h>
#include "fifo.h"

/*
 *  Lecturas o escritura de mas de BUF_LEN -> Error
 *  Al abrir un FIFO en lectura se BLOQUEA hasta habrir la escritura y viceversa
 *  El PRODUCTOR se BLOQUEA si no hay hueco
 *  El CONSUMIDOR se BLOQUEA si no tiene todo lo que pide
 *  Lectura a FIFO VACIO sin PRODUCTORES -> EOF 0
 *  Escritura a FIFO sin CONSUMIDOR -> Error
 */

DEFINE_SEMAPHORE(mutex_fifos);

static int Major;  

static struct file_operations fops = {
    .read = fifo_read,
    .write = fifo_write,
    .open = fifo_open,
    .release = fifo_release
};


int init_module(void)
{
    Major = register_chrdev(0, DEVICE_NAME, &fops);
    
    if (Major < 0) {
        printk(KERN_ALERT "Registering char device failed with %d\n", Major);
        return Major;
    }

    DBG("I was assigned major number %d. To talk to\n", Major);
    DBG("the driver, create two dev files with\n");
    DBG("'mknod /var/tmp/fifo -m 666 c %d 0'.\n", Major);
    DBG("'mknod /var/tmp/fifo -m 666 c %d 1'.\n", Major);
    DBG("Remove the devices files and module when done.\n");

    return 0;
}

void cleanup_module(void)
{
    unregister_chrdev(Major, DEVICE_NAME);
}



static int fifo_open(struct inode *inode, struct file *file)
{
    char is_cons = file->f_mode & FMODE_READ;
    char used = false;
    struct fifo_t *f = vmalloc(sizeof(fifo_t));
    
    if(f == NULL)
        return -ENOMEM;

    if(init_fifo(f) != 0){
        vfree(f);
        return -ENOMEM;
    }

    // Ya tenemos un nuevo fifo creado.
    if(down_interruptible(&mutex_fifos))
        return -EINTR;

    if(inode->i_private == NULL){
        inode->i_private = f;
        used = true;
    }
    up(&mutex_fifos);

    if(!used){ // Si no hemos usado el nuevo fivo (el inodo ya tenia fifo asociado)
        vfree(f);
        f = NULL;
    }

    f = (struct fifo_t*) inode->i_private;

    DBGV("Pipe abierto para %s con lecotres %d, escriores %d", 
            (file->f_mode & FMODE_READ)? "lectura": "escritura",
            f->num_cons, 
            f->num_prod);

    // INICIO SECCIÓN CRÍTICA >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
    if (down_interruptible(&f->mutex)) 
        return -EINTR;

    if (is_cons){ 
        // Eres consumidor
        f->num_cons++;
        while(f->num_bloq_prod){
            f->num_bloq_prod--;
            up(&f->cola_prod);
        }
        
        while(!f->num_prod)
            cond_wait(&f->mutex, &f->cola_cons, f->num_bloq_cons,
                __InterruptHandler__ { 
                    down(&f->mutex);
                    f->num_cons--;
                    up(&f->mutex);
                }
            );

    }else{ 
        // Eres un productor.
        f->num_prod++;
        while(f->num_bloq_cons){
            f->num_bloq_cons--;
            up(&f->cola_cons);
	    }
        
        while(!f->num_cons)
            cond_wait(&f->mutex, &f->cola_prod, f->num_bloq_prod,
                __InterruptHandler__ { 
                    down(&f->mutex);
                    f->num_prod--;
                    up(&f->mutex);
                }
            );
    }

    up(&f->mutex);
        
    // FIN SECCIÓN CRÍTICA <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
    try_module_get(THIS_MODULE);

    return 0;
}


static int fifo_release(struct inode *inode, struct file *file)
{
    struct fifo_t *f = (struct fifo_t*) inode->i_private;
    char removed = false;

    // INCIO SECCIÓN CRÍTICA >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
    if (down_interruptible(&f->mutex)) 
        return -EINTR;

    if (file->f_mode & FMODE_READ){
        f->num_cons--;
	if(f->num_cons == 0) // Por si hay productores durmiendo, levántalos. 
        while(f->num_bloq_prod){
	        f->num_bloq_prod--;
            cond_signal(&f->cola_prod);
	    }
    }else 
        f->num_prod--;


    if( !(f->num_prod || f->num_cons) ){
        // El último en salir limpia.
        inode->i_privete = NULL;
        removed = true;
    }
        
    up(&f->mutex);
    // FIN SECCIÓN CRÍTICA <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
    
    if(removed){ 
        destroy_fifo(f);
        vfree(f);
    }

    DBGV("Pipe de %s cerrado con lecotres %d, escriores %d", 
            (file->f_mode & FMODE_READ)? "lectura": "escritura",
            f->num_cons, 
            f->num_prod);

    module_put(THIS_MODULE);

    return 0;
}



static ssize_t fifo_read (struct file *filp,	
                            char __user *buff,	
                            size_t length,	
                            loff_t *offset)
{
    struct inode *inode = filp->f_dentry->d_inode;
    struct fifo_t *f = (struct fifo_t*) inode->i_private;
    char *kbuff;
    DBGV("Quiero leer %d bytes", length);
    DBGV("Escritores esperando %d", f->num_bloq_prod);

    if (length > BUF_LEN){
        DBG("[ERROR] Lectura demasiado grande");
        return -EINVAL;
    }

    if((kbuff = vmalloc(length)) == NULL){
        DBG("[ERROR] no se ha podido alojar memoria dinámica");
        return -ENOMEM;
    }

    // INICIO SECCIÓN CRÍTICA >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
    if (down_interruptible(&f->mutex)){ 
        DBGV("[INT] Interrumpido al intentar acceder a la SC");
        vfree(kbuff);
        return -EINTR;
    }

    // Si el pipe esta vacio y no hay productores -> EOF
    if (f->num_prod == 0 && is_empty_cbuffer_t(f->buff)){
        up(&f->mutex);
        DBGV("Pipe vacio sin productores");
        vfree(kbuff);
        return 0;
    }

    // El consumidor se bloquea si no tiene lo que pide
    while (size_cbuffer_t(f->buff) < length){
        cond_wait(&f->mutex, &f->cola_cons, f->num_bloq_cons,
                __InterruptHandler__ {
                    vfree(kbuff);
                });
    
        if (f->num_prod == 0 && is_empty_cbuffer_t(f->buff)){
            up(&f->mutex);
	    DBGV("Pipe vacio sin productores");
    	    vfree(kbuff);
	    return 0;
        }
    }

    remove_items_cbuffer_t(f->buff, kbuff, length); 
    
    // Broadcast a todos los productores, hay nuevos huecos
    while(f->num_bloq_prod){
        cond_signal(&f->cola_prod);
        f->num_bloq_prod--;
    }

    up(&f->mutex);
    // FIN SECCIÓN CRÍTICA <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

    length -= copy_to_user(buff, kbuff, length);
    
    DBGV("[TERMINADO] escritores esperando %d", num_bloq_prod);

    vfree(kbuff);
    return length;
}


static ssize_t fifo_write (struct file *filp, 
                            const char __user *buff, 
                            size_t length, 
                            loff_t *offset)
{
    struct inode *inode = filp->f_dentry->d_inode;
    struct fifo_t *f = (struct fifo_t*) inode->i_private;

    char *kbuff;

    DBGV("Quiero escribir %d bytes", length);

    if (length > BUF_LEN){
        DBG("[ERROR] Demasiado para escribir");
        return -EINVAL;
    }

    if((kbuff = vmalloc(length)) == NULL){
        DBG("[ERROR] no se ha podido alojar memoria dinámica");
        return -ENOMEM;
    }

    length -= copy_from_user(kbuff, buff, length);

    // INICIO SECCIÓN CRÍTICA >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
    if (down_interruptible(&f->mutex)){
        DBGV("[INT] Interrumpido al intentar acceder a la SC");
        vfree(kbuff);
        return -EINTR;
    }

    // Si escribe sin consumiedores -> Error
    if (f->num_cons == 0){
        up(&f->mutex);
        DBG("[ERROR] Escritura sin consumidor");
        vfree(kbuff);
        return -EPIPE;
    }

    // El productor se bloquea si no hay espacio
    while (f->num_cons > 0 && nr_gaps_cbuffer_t(f->buff) < length)
        cond_wait(&f->mutex, &f->cola_prod, f->num_bloq_prod,
                __InterruptHandler__ {
                    vfree(kbuff);
                });

    // Comprobamos que al salir de un posible wait sigue habiendo consumidores.
    if (f->num_cons == 0){
        up(&f->mutex);
	DBG("[ERROR] Escritura sin consumidor.");
	vfree(kbuff);
	return -EPIPE;
    }
    
    insert_items_cbuffer_t(f->buff, kbuff, length); 
    
    // Broadcast a todos los consumidores, ya hay algo.
    while(f->num_bloq_cons){
        cond_signal(&f->cola_cons);
        f->num_bloq_cons--;
    }

    up(&f->mutex);
    // FIN SECCIÓN CRÍTICA <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
    
    DBGV("[TERMINADO] lectores esperando %d", num_bloq_cons);

    vfree(kbuff);
    return length;
}
