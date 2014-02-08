#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "chat.h"

void* emisor(void *ptr){
    struct thread_args *args = (struct thread_args *)ptr;
    int pipe = open(args->canal, O_WRONLY);
    struct mensaje_t m; 
    char *line = NULL;
    int nread;
    size_t len;

    ptr = NULL;

    if(pipe == -1){
        fprintf(stderr, "Error al abrir el pipe\n");
        exit(EXIT_FAILURE);
    }

    strncpy(m.contenido, args->nombre, MAX_CHARS_MSG-1);
    m.tipo = MENSAJE_NOMBRE;

    write(pipe, &m, sizeof(struct mensaje_t));
    
    free(args);
    args = NULL;

    while((nread = getline(&line, &len, stdin)) != -1){
        strncpy(m.contenido, line, MAX_CHARS_MSG-1);
        m.tipo = MENSAJE_NORMAL;
        write(pipe, &m, sizeof(struct mensaje_t));
    }

    if(line != NULL)
        free(line);

    m.contenido[0] = 0;
    m.tipo = MENSAJE_FIN;
    write(pipe, &m, sizeof(struct mensaje_t));
    
    close(pipe);
    
    exit(EXIT_SUCCESS);
}

void* receptor(void *ptr){
    struct thread_args *args = (struct thread_args *) ptr;
    int pipe = open(args->canal, O_RDONLY);
    struct mensaje_t nombre, m;
    int nread = 0;
    free(args);
    args = NULL;
    ptr = NULL;
    
    if(pipe == -1){
        fprintf(stderr, "Error al abrir el pipe\n");
        exit(EXIT_FAILURE);
    }

    nread = read(pipe, &nombre, sizeof(struct mensaje_t));
    
    if(nombre.tipo != MENSAJE_NOMBRE){
        fprintf(stderr, "FALLO DE PROTOCOLO\n");
        return NULL;
    }

    printf("Conexión establecida con %s\n", nombre.contenido);
    
    while((nread = read(pipe, &m, sizeof(struct mensaje_t))) > 0){
        if(m.tipo == MENSAJE_NORMAL)
            printf("+%s dice: %s", nombre.contenido, m.contenido);
        else{
            fprintf(stderr,"Conexión cerrada por el otro extremo\n");
            break;
        }
    }

    close(pipe);

    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    struct thread_args *rec, *emi;
    pthread_t emith, recth;

    if (argc != 4){
        fprintf(stderr, "Usage: %s Nombre fifo_out fifo_in\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    rec = malloc(sizeof(struct thread_args));
    emi = malloc(sizeof(struct thread_args));
    
    if(rec == NULL || emi == NULL){
        fprintf(stderr, "Error alojar memoria\n");
        exit(EXIT_FAILURE);
    }
    
    rec->canal = argv[3];
    emi->canal = argv[2];
    emi->nombre = argv[1];
    
    printf("Iniciando chat...\n");

    if(pthread_create(&emith, NULL, emisor, (void *)emi) != 0){
        fprintf(stderr, "Error al crear hilo emisor\n");
        exit(EXIT_FAILURE);
    }

    if(pthread_create(&recth, NULL, receptor, (void *)rec) != 0){
        fprintf(stderr, "Error al crear hilo receptor\n");
        exit(EXIT_FAILURE);
    }

    // Espera a que terminen los dos
    pthread_join(emith, NULL);
    pthread_join(recth, NULL);

    printf("ADIOS!\n");

    exit(EXIT_SUCCESS);
}
