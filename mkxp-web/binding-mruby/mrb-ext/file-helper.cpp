
#include "file-helper.h"
#include <SDL_rwops.h>

#ifdef __EMSCRIPTEN__
#include "emscripten.hpp"
#endif

char* SDL_rw_file_helper::read() {

#ifdef __EMSCRIPTEN__
	load_file_async_js(filename);
#endif

        SDL_RWops *rw = SDL_RWFromFile(filename, "rb");
        if (rw == NULL) return NULL;

        Sint64 res_size = SDL_RWsize(rw);
        char* res = (char*)malloc(res_size + 1);

        Sint64 nb_read_total = 0, nb_read = 1;
        char* buf = res;
        while (nb_read_total < res_size && nb_read != 0) {
                nb_read = SDL_RWread(rw, buf, 1, (res_size - nb_read_total));
                nb_read_total += nb_read;
                buf += nb_read;
        }
        SDL_RWclose(rw);
        if (nb_read_total != res_size) {
                free(res);
                return NULL;
        }

        res[nb_read_total] = '\0';
        length = nb_read_total;
        return res;
}

bool SDL_rw_file_helper::write(char * data) {
        SDL_RWops *rw = SDL_RWFromFile(filename, "w");
        if(rw != NULL) {
		size_t len = SDL_strlen(data);
		if (SDL_RWwrite(rw, data, 1, len) != len) {
		        return false;
		} else {
		        return true;
		}
        	SDL_RWclose(rw);
        	return true;
        }
        return false;
}
