/*Non-Canonical Input Processing*/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#define BAUDRATE B38400
#define MODEMDEVICE "/dev/ttyS1"
#define _POSIX_SOURCE 1 /* POSIX compliant source */
#define FALSE 0
#define TRUE 1

#define FLAG 0x7E
#define A 0x03
#define SET_C 0x03
#define UA_C 0x07
#define UA_BCC (A^UA_C)
#define SET_BCC (A^SET_C)
#define NUMMAX 3
#define C10 0x00
#define C11 0x40
#define Escape 0x7D
#define escapeFlag 0x5E
#define escapeEscape 0x5D
#define C2Start 0x02
#define C2End 0x03
#define CRR0 0x05
#define CRR1 0x85
#define CREJ0 0x01
#define CREJ1 0x81
#define T1 0x00
#define T2 0x01
#define L1 0x04
#define L2 0x0B
#define DISC 0x0B
#define headerC 0x01
#define sizePacketConst 7

int sumAlarms=0;
int flagAlarm = FALSE;
int trama = 0;
int paragem = FALSE;
unsigned char numMensagens=0;
int numTotalTramas=0;

volatile int STOP=FALSE;

int LLOPEN(int fd, int x);

int LLWRITE(int fd, unsigned char* mensagem, int size);

unsigned char calculoBCC2(unsigned char* mensagem, int size);

void sendControlMessage(int fd, unsigned char C);

unsigned char* stuffingBCC2(unsigned char BCC2,int* sizeBCC2);

unsigned char* controlPackageI(unsigned char state, off_t sizeFile, unsigned char* fileName, int sizeOfFilename, int* sizeControlPackageI);

unsigned char readControlMessage(int fd);

unsigned char* openReadFile(unsigned char* fileName, off_t* sizeFile);

unsigned char* headerAL(unsigned char* mensagem, off_t sizeFile, int * sizePacket);

unsigned char* splitMensagem(unsigned char* mensagem,off_t* indice,int* sizePacket, off_t sizeFile);

void LLCLOSE(int fd);

//handler do sinal de alarme
void alarmHandler(){
  printf("Alarm=%d\n", sumAlarms+1);
  flagAlarm=TRUE;
  sumAlarms++;
}

int main(int argc,char** argv){
  int fd;
  struct termios oldtio,newtio;
  off_t sizeFile; //tamanho do ficheiro em bytes
  off_t indice=0;
  int sizeControlPackageI=0;


  if ( (argc < 3) ||
  ((strcmp("/dev/ttyS0", argv[1])!=0) &&
  (strcmp("/dev/ttyS1", argv[1])!=0) )) {
    printf("Usage:\tnserial SerialPort\n\tex: nserial /dev/ttyS1\n");
    exit(1);
  }
  /*
  Open serial port device for reading and writing and not as controlling tty
  because we don't want to get killed if linenoise sends CTRL-C.
  */
  fd = open(argv[1], O_RDWR | O_NOCTTY );
  if (fd <0) {perror(argv[1]); exit(-1); }

  if ( tcgetattr(fd,&oldtio) == -1) { /* save current port settings */
    perror("tcgetattr");
    exit(-1);
  }

  bzero(&newtio, sizeof(newtio));
  newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
  newtio.c_iflag = IGNPAR;
  newtio.c_oflag = 0;

  /* set input mode (non-canonical, no echo,...) */
  newtio.c_lflag = 0;

  newtio.c_cc[VTIME]    = 1;   /* inter-unsigned character timer unused */
  newtio.c_cc[VMIN]     = 0;   /* blocking read until 5 unsigned chars received */

  /*
  VTIME e VMIN devem ser alterados de forma a proteger com um temporizador a
  leitura do(s) pr�ximo(s) caracter(es)
  */

  tcflush(fd, TCIOFLUSH);

  if ( tcsetattr(fd,TCSANOW,&newtio) == -1) {
    perror("tcsetattr");
    exit(-1);
  }

  printf("New termios structure set\n");

  // instalar handler do alarme
  (void)signal(SIGALRM,alarmHandler);

 unsigned char * mensagem = openReadFile((unsigned char*)argv[2], &sizeFile);

  //se nao conseguirmos efetuar a ligaçao atraves do set e do ua o programa termina
  if(!LLOPEN(fd,0)){
    return -1;
  }

  int sizeOfFileName = strlen(argv[2]);
  unsigned char * fileName = (unsigned char*) malloc(sizeOfFileName);
  fileName = (unsigned char*)argv[2];
  unsigned char * start = controlPackageI(C2Start, sizeFile, fileName, sizeOfFileName, &sizeControlPackageI);

  LLWRITE(fd,start,sizeControlPackageI);
  printf("Mandou trama START\n");

  int sizePacket = sizePacketConst;

  while(sizePacket == sizePacketConst && indice < sizeFile){
    //split mensagem
    unsigned char* packet=splitMensagem(mensagem,&indice,&sizePacket,sizeFile);
    printf("Mandou packet numero %d\n", numTotalTramas);
    //header nivel aplicação
    int headerSize = sizePacket;
    unsigned char* mensagemHeader = headerAL(packet,sizeFile,&headerSize);
    //envia a mensagem
    if(!LLWRITE(fd,mensagemHeader,headerSize)){
      printf("Limite de alarmes atingido\n");
      return -1;
    }
  }

  unsigned char * end = controlPackageI(C2End, sizeFile, fileName, sizeOfFileName, &sizeControlPackageI);
  LLWRITE(fd,end,sizeControlPackageI);
  printf("Mandou trama END\n");

  LLCLOSE(fd);
  sleep(1);

  if ( tcsetattr(fd,TCSANOW,&oldtio) == -1) {
    perror("tcsetattr");
    exit(-1);
  }

  close(fd);
  return 0;
}

//cabecalho das tramas i
//Application layer
unsigned char* headerAL(unsigned char* mensagem,off_t sizeFile,int* sizePacket){
  unsigned char* mensagemFinal = (unsigned char*)malloc(sizeFile + 4);
  mensagemFinal[0] = headerC;
  mensagemFinal[1]=numMensagens%255;
  mensagemFinal[2]=(int)sizeFile/256;
  mensagemFinal[3]=(int)sizeFile%256;
  memcpy(mensagemFinal+4,mensagem,*sizePacket);
  *sizePacket += 4;
  numMensagens++;
  numTotalTramas++;
  return mensagemFinal;
}

//divide uma mensagem proveniente do ficheiro em packets
//Application layer
unsigned char* splitMensagem(unsigned char* mensagem,off_t* indice,int* sizePacket, off_t sizeFile){
  unsigned char* packet;
  int i =0;
  off_t j = *indice;
  if(*indice+*sizePacket > sizeFile){
    *sizePacket = sizeFile - *indice;
  }
  packet = (unsigned char*)malloc(*sizePacket);
  for(; i < *sizePacket;i++,j++){
    packet[i]=mensagem[j];
  }
  *indice = j;
  return packet;
}

//vê se o UA foi recebido.
//Data link layer
void stateMachineUA(int* state, unsigned char* c){

  switch(*state){
        //recebe flag
        case 0:
          if(*c==FLAG)
            *state=1;
          break;
        //recebe A
        case 1:
          if(*c==A)
            *state=2;
          else
            {
              if(*c==FLAG)
                *state=1;
              else
                *state = 0;
            }
          break;
        //recebe C
        case 2:
          if(*c==UA_C)
            *state=3;
          else{
            if(*c==FLAG)
              *state=1;
            else
              *state = 0;
          }
        break;
        //recebe BCC
        case 3:
          if(*c == UA_BCC)
            *state = 4;
          else
            *state=0;
        break;
        //recebe FLAG final
        case 4:
          if(*c==FLAG){
            paragem=TRUE;
            alarm(0);
            printf("Recebeu UA\n");
          }
          else
            *state = 0;
        break;
      }

}

//manda a trama SET através da porta série para o recetor, para "avisar" que vai começar a mandar dados
//Data link layer
int LLOPEN(int fd, int x){

	unsigned char c;
  do {
    sendControlMessage(fd,SET_C);
    alarm(3);
    flagAlarm=0;
    int state=0;

    while(!paragem && !flagAlarm){
      read(fd,&c,1);
      stateMachineUA(&state, &c);
    }
  } while(flagAlarm && sumAlarms < NUMMAX);
  printf("flag alarm %d\n",flagAlarm);
  printf("soma %d\n",sumAlarms );
  if(flagAlarm && sumAlarms==3){
    return FALSE;
  }
  else{
    flagAlarm=FALSE;
    sumAlarms=0;
    return TRUE;
  }
}

//faz stuffing de uma mensagem e, de seguida, manda-a através da porta série.
//data link layer
int LLWRITE(int fd, unsigned char* mensagem, int size){
  unsigned char BCC2;
  unsigned char* BCC2Stuffed = (unsigned char*)malloc(sizeof(unsigned char));
  unsigned char* mensagemFinal = (unsigned char*)malloc((size+6)*sizeof(unsigned char));
  int sizeMensagemFinal = size + 6;
  int sizeBCC2=1;
  BCC2 = calculoBCC2(mensagem,size);
  BCC2Stuffed = stuffingBCC2(BCC2,&sizeBCC2);
  int rejeitado = FALSE;

  mensagemFinal[0]= FLAG;
  mensagemFinal[1]=A;
  if(trama == 0){
    mensagemFinal[2]=C10;
  } else{
    mensagemFinal[2]=C11;
  }
  mensagemFinal[3] = (mensagemFinal[1]^mensagemFinal[2]);

  int i=0;
  int j=4;
  for(; i < size; i++){
    if(mensagem[i] == FLAG){
      mensagemFinal = (unsigned char*)realloc(mensagemFinal, ++sizeMensagemFinal);
      mensagemFinal[j]=Escape;
      mensagemFinal[j+1]=escapeFlag;
      j = j+2;
    }
    else{
      if(mensagem[i]==Escape){
        mensagemFinal = (unsigned char*)realloc(mensagemFinal, ++sizeMensagemFinal);
        mensagemFinal[j]=Escape;
        mensagemFinal[j+1]=escapeEscape;
        j = j+2;
      }
      else {
        mensagemFinal[j] = mensagem[i];
        j++;
      }
    }
  }

  if(sizeBCC2 == 1)
    mensagemFinal[j]=BCC2;
  else{
    mensagemFinal = (unsigned char*)realloc(mensagemFinal, ++sizeMensagemFinal);
    mensagemFinal[j]=BCC2Stuffed[0];
    mensagemFinal[j+1]=BCC2Stuffed[1];
    j++;
  }
  mensagemFinal[j+1]=FLAG;

  //mandar mensagem
  do{

    write(fd,mensagemFinal,sizeMensagemFinal);
    flagAlarm=FALSE;
    alarm(3);
    unsigned char C = readControlMessage(fd);
    if((C==CRR1 && trama == 0) || (C==CRR0 && trama == 1) ){
      printf("Recebeu rr %x, trama = %d\n", C, trama);
      rejeitado=FALSE;
      sumAlarms=0;
      trama ^= 1;
      alarm(0);
    }
    else {
      if(C== CREJ1 || C==CREJ0){
        rejeitado = TRUE;
        printf("Recebeu rej %x, trama=%d\n",C,trama);
        alarm(0);
      }
    }
  } while((flagAlarm && sumAlarms < NUMMAX) || rejeitado);
  if(sumAlarms >= NUMMAX)
    return FALSE;
    else
    return TRUE;
}

//manda uma qualquer trama de controlo
//Application layer
void sendControlMessage(int fd, unsigned char C){
  unsigned char message[5];
  message[0]=FLAG;
  message[1]=A;
  message[2]=C;
  message[3]=message[1]^message[2];
  message[4]=FLAG;
  write(fd,message,5);
}

//lê uma qualquer trama de controlo
//Data link layer
unsigned char readControlMessage(int fd){
  int state=0;
  unsigned char c;
  unsigned char C;

  while(!flagAlarm && state!=5){
    read(fd,&c,1);
    switch(state){
      //recebe FLAG
      case 0:
      if(c==FLAG)
        state=1;
        break;
      //recebe A
      case 1:
      if(c==A)
        state=2;
      else{
        if(c==FLAG)
          state=1;
        else
          state=0;
      }
      break;
      //recebe c
      case 2:
        if(c==CRR0 || c==CRR1 || c==CREJ0 ||  c==CREJ1 || c==DISC){
          C=c;
          state=3;
        }
        else{
          if(c==FLAG)
            state=1;
          else
            state=0;
        }
        break;
      //recebe BCC
      case 3:
      if(c==(A^C))
        state=4;
      else
        state=0;
      break;
      //recebe FLAG final
      case 4:
        if(c==FLAG)
          {
            alarm(0);
            state=5;
            return C;
          }
        else
          state=0;
      break;
    }
  }
  return 0xFF;
}

//calculo do BCC2 de uma mensagem
//Data link layer
unsigned char calculoBCC2(unsigned char* mensagem, int size){
  unsigned char BCC2 = mensagem[0];
  int i;
  for(i=1; i < size; i++){
    BCC2^=mensagem[i];
  }
  return BCC2;
}

//stuffing do BCC2
//Data link layer
unsigned char* stuffingBCC2(unsigned char BCC2, int* sizeBCC2){
  unsigned char* BCC2Stuffed;
  if(BCC2 == FLAG){
      BCC2Stuffed= (unsigned char*)malloc(2*sizeof(unsigned char*));
      BCC2Stuffed[0]=Escape;
      BCC2Stuffed[1]=escapeFlag;
      (*sizeBCC2)++;
  }
  else{
    if(BCC2==Escape){
      BCC2Stuffed= (unsigned char*)malloc(2*sizeof(unsigned char*));
      BCC2Stuffed[0]=Escape;
      BCC2Stuffed[1]=escapeEscape;
      (*sizeBCC2)++;
    }
  }

  return BCC2Stuffed;
}

//abre o ficheiro
unsigned char* openReadFile(unsigned char* fileName, off_t* sizeFile){
  FILE* f;
  struct stat metadata;
  unsigned char* fileData;

  if((f = fopen((char*)fileName, "rb"))== NULL){
    perror("error opening file!");
    exit(-1);
  }
  stat((char*)fileName,&metadata);
  (*sizeFile) = metadata.st_size;
  printf("This file has %ld bytes \n",*sizeFile);

  fileData = (unsigned char*)malloc(*sizeFile);

  fread(fileData,sizeof(unsigned char),*sizeFile,f);
  return fileData;
}

//manda a trama start e a end
//Application layer
unsigned char* controlPackageI(unsigned char state, off_t sizeFile, unsigned char* fileName, int sizeOfFileName, int* sizeControlPackageI){
    *sizeControlPackageI = 9*sizeof(unsigned char)+sizeOfFileName;
  unsigned char* package = (unsigned char*)malloc(*sizeControlPackageI);

  if(state==C2Start)
    package[0]=C2Start;
  else
    package[0]=C2End;
  package[1]=T1;
  package[2]=L1;
  package[3]= (sizeFile >> 24) & 0xFF;
  package[4]= (sizeFile >> 16) & 0xFF;
  package[5]= (sizeFile >> 8) & 0xFF;
  package[6]= sizeFile & 0xFF;
  package[7]=T2;
  package[8]=sizeOfFileName;
  int k =0;
  for(; k < sizeOfFileName;k++){
    package[9+k]=fileName[k];
  }
  return package;
}

//manda disconnect
//Data link layer
void LLCLOSE(int fd){
  sendControlMessage(fd,DISC);
  printf("Mandou DISC\n");
  unsigned char C;
  //espera ler o DISC
  C = readControlMessage(fd);
  while(C!=DISC){
    C = readControlMessage(fd);
  }
  printf("Leu disc\n");
  sendControlMessage(fd,UA_C);
  printf("Mandou ua final\n");
}
