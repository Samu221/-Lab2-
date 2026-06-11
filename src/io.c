#include "io.h"

#include <errno.h>

ssize_t readn(int fd, void *buf, size_t n) {
    size_t left = n;    //byte da leggere
    ssize_t r;          //letti
    char *p = buf;      //puntatore a inizio buffer

    while (left > 0) {
        if ((r = read(fd, p, left)) == -1) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) break;      //se ho finito di leggere esco
        left -= r;      //diminuisco restanti da leggere   
        p += r;     //sposto il puntatore
    }

    return n - left;
}

ssize_t writen(int fd, const void *buf, size_t n) {
    size_t left = n;    //byte da scrivere
    ssize_t r;          //scritti
    const char *p = buf;    //puntatore dentro buffer

    while (left > 0) {  //finche non ho finito
        if ((r = write(fd, p, left)) == -1) { //scrivo r byte 
            if (errno == EINTR) continue;
            return -1;
        }
        left -= r;  //diminuisco restanti da scrivere
        p += r;     //sposto il puntatore
    }

    return n;
}