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

#define SEGMENT_SIZE (2)
#define MAX_BLK_SIZE 64

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

int erase_all(int fh) {
  int ln, cnt=0;
  uint8_t buff[2]={0x03,0x00};
  uint8_t buf[2] = { 0x00, 0x00 };

  if ((ln = write(fh, buff, 2)) < 0) {
        fprintf(stderr,"ERROR1: %s len:%d\n", strerror(errno), ln);
        return -1;
  }

  fprintf(stderr,"Erasing in-progess...\n");
  do {
    usleep(200000);
    buf[0] = 0x00;
    if ((ln = read(fh, buf, 1)) < 0) {
      fprintf(stderr,"ERROR2: %s len:%d\n", strerror(errno), ln);
      return -1;
    }
    fprintf(stderr,".%s", cnt % 64 == 63 ? "\n" : "");
    cnt++;
  } while(buf[0] != 0xff && cnt < 500);
  fprintf(stderr,"\n");
  return (buf[0]==0xff && cnt < 500);
}



int write_segment(int fh, uint32_t offset, uint8_t  *buf, int size) {
  uint8_t buff[4+SEGMENT_SIZE+1];
  int ln;
  buff[0] = 0x02;
  buff[1] = (offset >> 16) & 0x00ff;
  buff[2] = (offset >> 8) & 0x00ff;
  buff[3] = (offset) & 0x00ff;
  int i;
  for(i=0;i<size;i++)
    buff[4+i] = buf[i];
  buff[4+i] = 0x00;

  if ((ln = write(fh, buff, size+4+1)) < 0) {
    fprintf(stderr,"ERROR1: %s len:%d\n", strerror(errno), ln);
    return -1;
  }

  usleep(20);
  return (ln == size+4+1) ? size : -1;
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
    if(flash_size <= 0) {
        fprintf(stderr,"ERROR: Wrong EEPROM ID or flash size: %d!\n", flash_size);
        close(fd);
        exit(1);
    }

    fprintf(stderr,"Found %d mbit flash\n", flash_size*8/(1024*1024));

    char *line = NULL;
    size_t len = 0;
    ssize_t read = 0;
    printf("All flash contents will be ERASED, Are You sure? [y/N] ");
    read = getline(&line, &len, stdin);
    puts("");
    if(read < 0 || !line || len < 1 || (line[0] != 'y' && line[0] != 'Y')) {
        fprintf(stderr,"Aborted by user\n");
        exit(1);
    }
    free(line);

    if(erase_all(fd)!=1){
        fprintf(stderr,"ERROR: Can't erase!\n");
        close(fd);
        exit(1);
    }

    FILE *i_f = fopen(argv[1], "rb");
    fprintf(stderr,"Writing to EEPROM...\n");
    uint32_t offset=0;

    int loops=0;
    while(offset < flash_size) {
        if((loops % 64) == 63)      fprintf(stderr, ".");
        if((loops % 4096) == 4095)  fprintf(stderr, ": %d%%\n", 100*offset/flash_size);

        if((ret = fread(buf, 1, SEGMENT_SIZE, i_f)) !=  SEGMENT_SIZE) { printf("Couldn't read file! %d\n", ret); return 1; }
        if((ret = write_segment(fd, offset/2, buf, SEGMENT_SIZE)) < 0) { printf("Couldn't write device! %d\n", ret); return 1; }
        offset+=ret;
        loops++;
    }
    fprintf(stderr,"\n");
    fclose(i_f);
    close(fd);
}
