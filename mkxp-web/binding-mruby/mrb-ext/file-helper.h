struct SDL_rw_file_helper {
    const char * filename;
    char * read();
    int length;
    bool write(char * data);
};