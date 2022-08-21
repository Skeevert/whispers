#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BIT_DEPTH_OFFSET 34
#define SUBCHUNK_1_OFFSET 12
#define BUFFSIZE 1024
#define PCM_OFFSET 20
#define EXTRA_PARAM_OFFSET 36

// I am using int32_t instead of size_t because wav limits section sizes to 4 bytes
int32_t
find_data_offset (int wav_fd)
{
    if (lseek(wav_fd, SUBCHUNK_1_OFFSET, SEEK_SET) == -1) {
        close(wav_fd);
        exit(-1);
    }

    int32_t offset = SUBCHUNK_1_OFFSET;

    char chunk_id[5];
    int32_t chunk_len = 0;

    memset(chunk_id, 0, 5);


    while (strncmp(chunk_id, "data", 4)) {
        if ((offset = lseek(wav_fd, chunk_len, SEEK_CUR)) == -1) {
            close(wav_fd);
            exit(-1);
        }

        read(wav_fd, chunk_id, 4);
        read(wav_fd, &chunk_len, 4);
    }

    return offset;
}

void *
xmalloc (size_t size)
{
    void* allocated = malloc(size);
    if (!allocated) {
        exit(-1);
    }

    return allocated;
}


    // Encoded data length to look less sensible
uint32_t
payload_length_encoded (int payload_fd)
{
    uint32_t payload_len = 0;
    uint32_t bytes_read;
    char buffer[BUFFSIZE];
    while ((bytes_read = read(payload_fd, buffer, BUFFSIZE)) > 0) {
        payload_len += bytes_read;
    }

    if (lseek(payload_fd, 0, SEEK_SET) == -1) {
        return 0;
    }

    uint32_t payload_start;
    read(payload_fd, &payload_start, 4);

    if (lseek(payload_fd, 0, SEEK_SET) == -1) {
        return 0;
    }

    printf("Length to encode: %u\n", payload_len);
    return payload_start ^ payload_len;
}

int32_t
encode (int16_t bit_depth, int32_t data_size, char** av, int32_t data_start_offset)
{
    int wav_fd = open(av[2], O_RDONLY);

    char* header = (char *)xmalloc(data_start_offset);
    read(wav_fd, header, data_start_offset);
  
    int dest_fd = open(av[3], O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    int payload_fd = open(av[1], O_RDONLY);

    write(dest_fd, header, data_start_offset);

    int32_t sample_bytes = bit_depth / CHAR_BIT;
    int32_t current_data = 0;
    int32_t data_end_offset = data_start_offset + data_size;
    int32_t current_offset = data_start_offset;
    int32_t bytes_encoded = 0;
    uint8_t payload_byte = 0;
    uint8_t finished = 0;

    // calculate encoded data length
    uint32_t initial_i = 0;
    uint32_t payload_len = payload_length_encoded(payload_fd);
    uint8_t len_byte = 0;

    while (initial_i < sizeof(payload_len)) {
        len_byte = payload_len & 255;
        payload_len >>= 8; // Shift to next byte
        current_offset += read(wav_fd, &current_data, sample_bytes);
        current_data &= -256;
        current_data |= len_byte;

        write(dest_fd, &current_data, sample_bytes);
        initial_i++;
    }


    while (current_offset < data_end_offset) {
        if (!read(payload_fd, &payload_byte, 1)) {
            finished = 1;
        }
        if (finished) {
            // Some random lmao
            payload_byte = rand() % 256;
        } else {
            bytes_encoded++;
        }
        current_offset += read(wav_fd, &current_data, sample_bytes);
        current_data &= -256;
        current_data |= payload_byte;

        write(dest_fd, &current_data, sample_bytes);
    }

    char write_buffer[BUFFSIZE];
    size_t bytes_read = 0;

    while ((bytes_read = read(wav_fd, write_buffer, BUFFSIZE)) > 0) {
        write(dest_fd, write_buffer, bytes_read);
    }

    close(wav_fd);
    close(payload_fd);
    close(dest_fd);

    free(header);

    return bytes_encoded;
}

int
main (int ac, char **av)
{
    if (ac != 4) {
        printf("Specify payload file, wav file, destination file\n");
        return -1;
    }
    const char* wav_filename = av[2];

    int fd = open(wav_filename, O_RDONLY);
    int16_t bit_depth = 0;
    int32_t data_size = 0;

   
    int32_t actual_data_offset = find_data_offset(fd);
    int32_t actual_data_size_offset = actual_data_offset + 4; // Skipping data id
    printf("Actual data offset: %d\n", actual_data_offset);

    if (lseek(fd, BIT_DEPTH_OFFSET, SEEK_SET) == -1) {
        close(fd);
        return -1;
    }

    read(fd, &bit_depth, sizeof(bit_depth));
    printf("Bit depth: %d\n", bit_depth);

    if (lseek(fd, PCM_OFFSET, SEEK_SET) == -1) {
        close(fd);
        return -1;
    }

    int16_t audio_format = 1;
    read(fd, &audio_format, sizeof(audio_format));

    if (audio_format != 1) {
        printf("PCM is not 1, things may go wrong\n");
    }

    if (lseek(fd, actual_data_size_offset, SEEK_SET) == -1) {
        close(fd);
        return -1;
    }

    read(fd, &data_size, sizeof(data_size));
    printf("Data section size: %d\n", data_size);

    if (bit_depth >= 16) {
        printf("Encoding...\n");
        int32_t bytes_whispered = encode(bit_depth, data_size, av, actual_data_offset + 8); // Skipping id and size
        printf("Encode complete, whispered %d bytes\n", bytes_whispered);
    } else {
        printf("Bit depth is less than 16, aborting\n");
    }

    close(fd);
}
