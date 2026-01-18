/*
** A utility to dump the entire contents of a GDBM table in a 
** readable format.
*/
/*
** 中文说明:
** 本文件实现 gdbmdump 小工具，用于将 GDBM 数据库文件的键和值以
** 十六进制和可读字符的形式完整地打印出来，便于调试与检查。
*/
#include <stdio.h>
#include <ctype.h>
#include <gdbm.h>
#include <stdlib.h>

static void print_data(char *zPrefix, datum p){
  int i, j;

  printf("%-5s: ", zPrefix);
  for(i=0; i<p.dsize; i+=20){
    for(j=i; j<p.dsize && j<i+20; j++){
      printf("%02x", 0xff & p.dptr[j]);
      if( (j&3)==3 ) printf(" ");
    }
    while( j<i+20 ){
      printf("  ");
      if( (j&3)==3 ) printf(" ");
      j++;
    }
    printf(" ");
    for(j=i; j<p.dsize && j<i+20; j++){
      int c = p.dptr[j];
      if( !isprint(c) ){ c = '.'; }
      putchar(c);
    }
    printf("\n");
    if( i+20<p.dsize ) printf("       ");
  }
}

static int gdbm_dump(char *zFilename){
  GDBM_FILE p;
  datum data, key, next;

  p = gdbm_open(zFilename, 0, GDBM_READER, 0, 0);
  if( p==0 ){
    fprintf(stderr,"can't open file \"%s\"\n", zFilename);
    return 1;
  }
  key = gdbm_firstkey(p);
  while( key.dptr ){
    print_data("key",key);
    data = gdbm_fetch(p, key);
    if( data.dptr ){
      print_data("data",data);
      free( data.dptr );
    }
    next = gdbm_nextkey(p, key);
    free( key.dptr );
    key = next;
    printf("\n");
  }
  gdbm_close(p);
  return 0;
}

int main(int argc, char **argv){
  int i;
  int nErr = 0;
  for(i=1; i<argc; i++){
    nErr += gdbm_dump(argv[i]);
  }
  return nErr;
}
