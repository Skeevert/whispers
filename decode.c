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
#define PCM_OFFSET 20
#define BUFFSIZE 1024

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

uint32_t
payload_decode_length(int wav_fd, int32_t sample_bytes, int32_t data_start_offset)
{
    uint32_t data_length_encoded = 0;
    uint32_t len_salt = 0;

    // We are already at DATA_OFFSET
    uint32_t i = 0;
    int32_t current_data = 0;
    int32_t payload_byte = 0;
    int32_t extra_offset = 0;

    while (i < sizeof(data_length_encoded)) {
        extra_offset += read(wav_fd, &current_data, sample_bytes);
        payload_byte = current_data & 255;
        data_length_encoded |= (payload_byte << (8 * i));
        i++;
    }

    i = 0;
    current_data = 0;
    while (i < sizeof(len_salt)) {
        read(wav_fd, &current_data, sample_bytes);
        payload_byte = current_data & 255;
        len_salt |= (payload_byte << (8 * i));
        i++;
    }

    if (lseek(wav_fd, data_start_offset + extra_offset, SEEK_SET) == -1) {
        return 0;
    }

    printf("Length to decode: %u\n", data_length_encoded ^ len_salt);
    return data_length_encoded ^ len_salt;
}
    

uint32_t
decode (int16_t bit_depth, int32_t data_size, char** av, int32_t data_start_offset)
{
    int wav_fd = open(av[1], O_RDONLY);

    if (lseek(wav_fd, data_start_offset, SEEK_SET) == -1) {
        close(wav_fd);
        return 0;
    }

    int dest_fd = open(av[2], O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);

    int32_t sample_bytes = bit_depth / CHAR_BIT;
    int32_t current_data = 0;
    int32_t data_end_offset = data_start_offset + data_size;
    int32_t current_offset = data_start_offset;
    uint32_t bytes_decoded = 0;
    uint32_t payload_byte = 0;

    uint32_t data_len = payload_decode_length(wav_fd, sample_bytes, data_start_offset);

    while (current_offset < data_end_offset && bytes_decoded < data_len) {
        current_offset += read(wav_fd, &current_data, sample_bytes);
        payload_byte = current_data & 255;

        write(dest_fd, &payload_byte, 1);
        bytes_decoded++;
    }

    close(wav_fd);
    close(dest_fd);

    return bytes_decoded;
}

int
main (int ac, char **av)
{
    if (ac != 3) {
        printf("Specify wav file, destination file\n");
        return -1;
    }
    const char* wav_filename = av[1];

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
        printf("Decoding...\n");
        int32_t bytes_heard = decode(bit_depth, data_size, av, actual_data_offset + 8);
        printf("Decode complete, heard %d bytes\n", bytes_heard);
    } else {
        printf("Bit depth is less than 16, aborting\n");
    }

    close(fd);
}
