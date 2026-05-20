#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>


int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb");

    uint64_t token_len, result_len;

    while (fread(&token_len, sizeof(uint64_t), 1, f) == 1) {

        char *token = malloc(token_len + 1);
        fread(token, 1, token_len, f);
        token[token_len] = '\0';

        fread(&result_len, sizeof(uint64_t), 1, f);

        char *result = malloc(result_len + 1);
        fread(result, 1, result_len, f);
        result[result_len] = '\0';


        printf("[%"PRIu64"][%s][%"PRIu64"][%s] \n",token_len, token, result_len, result);

        free(token);
        free(result);
    }

    fclose(f);
}