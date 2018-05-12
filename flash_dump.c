#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define MAX_BLK_SIZE 64
#define SEGMENT_SIZE (16)


int flash_size_arr[4] = { 8*1024*1024/8, 16*1024*1024/8, 32*1024*1024/8, 4*1024*1024/8 };

int read_id(int fh) {
  int ln;
  int n;
  uint8_t buff[2]={0x05,0x00};
  uint8_t buf[4] = {0xFF, 0xFF, 0xFF, 0xFF};

  if ((ln = write(fh, buff, 2)) < 0) {
        fprintf(stderr,"ERROR1: %s len:%d\n", strerror(errno), ln);
  }

  if ((ln = read(fh, buf, 4)) < 0) {
        fprintf(stderr,"ERROR2: %s len:%d\n", strerror(errno), ln);
  }

  printf("ID[%d]: ", ln);
  for(n=0; n<ln; n++)
    printf("%02x",buf[n]);
  printf("\n");

  if(buf[0]!=0x1e || ((buf[1] & 0xF0) !=0xA0) || ((buf[1] & 0x0F) > 3) || buf[2] !=0x00)
    return -1;

  switch(buf[3]) {
    case 0xA3: printf("This flash is for ALTERA FPGAs\n"); break;
    case 0xC3: printf("This flash is for XILINX FPGAs\n"); break;
    default: printf("This flash is for UNKNOWN FPGAs\n"); break;
  }

  return flash_size_arr[(buf[1] & 0x0F)];
}

int read_segment(int fh, uint32_t offset, uint8_t  *buf, int size) {
  uint8_t buff[5]={0x01,0x00,0x00,0x00,0x00};
  int ln;
  buff[1] = (offset >> 16) & 0x00ff;
  buff[2] = (offset >> 8) & 0x00ff;
  buff[3] = (offset) & 0x00ff;

  if ((ln = write(fh, buff, 5)) < 0) {
    fprintf(stderr,"ERROR1: %s len:%d\n", strerror(errno), ln);
    return -1;
  }

  if ((ln = read(fh, buf, size)) < 0) {
    fprintf(stderr,"ERROR2: %s len:%d\n", strerror(errno), ln);
    return -1;
  }
  return ln;
}

int main(int argc, char *argv[]) {

    uint8_t buf[SEGMENT_SIZE];
    int fd, ret;


    if(argc<2) {
        fprintf(stderr,"ERROR: specify out file!\n");
        exit(1);
    }

    if ((fd = open(BUSNAME_STRING, O_RDWR)) < 0) {  printf("Couldn't open device! %d\n", fd); return 1; }
    if (ioctl(fd, I2C_SLAVE, 0x57) < 0)     { printf("Couldn't find device on address!\n"); return 1; }
    int flash_size = read_id(fd);
    if(flash_size <= 0){
        fprintf(stderr,"ERROR: Wrong EEPROM ID or flash size: %d!\n", flash_size);
        close(fd);
        exit(1);
    }

    fprintf(stderr,"Found %d mbit flash\n", flash_size*8/(1024*1024));

    FILE *of = fopen(argv[1], "wb");
    fprintf(stderr,"Reading EEPROM contents...\n");
    uint32_t offset=0;

    int loops=0;
    while(offset < flash_size) {
        if((loops % 64) == 63)      fprintf(stderr, ".");
        if((loops % 4096) == 4095)  fprintf(stderr, ": %d%%\n", 100*offset/flash_size);

        if((ret = read_segment(fd, offset/2, buf, SEGMENT_SIZE)) < 0) { printf("Couldn't read device! %d\n", ret); return 1; }
        fwrite(buf, ret, 1, of);
        offset+=ret;
        loops++;
    }
    fprintf(stderr,"\nFlash readed successfully!\n");
    fclose(of);
    close(fd);
}

