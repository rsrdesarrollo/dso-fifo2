
#define MAX_CHARS_MSG 128
enum tipo_mensaje {
    MENSAJE_NORMAL,
    MENSAJE_NOMBRE,
    MENSAJE_FIN
};

struct mensaje_t {
    char contenido[MAX_CHARS_MSG];
    enum tipo_mensaje tipo;
};

struct thread_args {
    char *canal;
    char *nombre;
};
