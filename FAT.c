#include "newtypes.h"
#include "spi_sd.h"
#include "FAT.h"
#include "AUDIO.h"
#include "altera_up_avalon_audio_regs_dgz.h"
#include "altera_up_avalon_audio_dgz.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <alt_types.h>
#include "io.h"
#include "system.h"

//list of command types
const command list[] =
{
	{"ls",list_directory},
	{"cd",change_directory},
	{"play",play_file}
	//{NULL,NULL}
};

euint32 uart_file;

SDfolder root;
euint32 LBAbegin;
SDfolder currDir;
part_boot Part_Boot;
euint32 FAT_start;
euint32 clust_start;

euint32 main(void)
{
	//initialise variables
	euint8 buffersd[size];
	euint32 i;
	currDir.ParentAdd = 0;
	currDir.name[0] = '/';
	currDir.name[1] = 0;
	currDir.startH = 0;
	currDir.startL = 2;
	root.ParentAdd = 0;
	root.name[0] = '/';
	root.name[1] = 0;
	root.startH = 0;
	root.startL = 2;

	audio_initialise();

	uart_file = open("/dev/uart_0",O_RDWR);
	// Prepare UART for command input
	printf("UART Ready.\n");
	UART_write("UART Ready.\r\n");
	// Initialise file system
	if(SD_init()==1)
		{
			printf("Could not open filesystem.\n");
			UART_write("File System could not mount.\r\n\t");
		}
		else
		{
			//FS mounted
			printf("File System Ready.\r\n");
			UART_write("File System Ready.\r\n");
		}
		
	read_mboot(buffersd);
	//calculates LBAbegin
	for(i=454;i<459;i++)
	{
		*((euint8*)&LBAbegin+(i-454)) = buffersd[i];
	}

	read_part_boot(LBAbegin,&Part_Boot); //reads the partition boot and fills the global variable
	char buffer[128];
	char* array[10];
	char* temp[10];
	while(1)
	{
		UART_write(">");
		if(uart_file == 0)
		{
			printf("Sorry UART open failed\r\n");
			return(-1);//error
		}
		euint32 number;
		euint32 xx=0;
		for(xx=0;xx<128;xx++)
		{
			buffer[xx]=0;//empty buffer
		}
		for(xx=0;xx<10;xx++)
		{
			array[xx] = 0;
			temp[xx]=0;
		}
		UARTListener(&buffer,uart_file);
		number = string_parser(buffer,array);
		euint32 i = 0;
		// search through commands to find match
		if(array[0]!=0)
		{
			for(i=0;i<3;i++)
			{
				//if string match found
				 if (strcompare(array[0],list[i].com_string)==0)
				{
					//extract the arguments of command
					euint32 j;
					for( j=0;j<number-1;j++)
					{
						temp[j] = array[j+1];
						//free(array[j+1]);
					}
					//call relative function
					list[i].com_fun((number-1),temp);
					for( j=0;j<number-1;j++)
					{
						free(array[j+1]);
					}
					i=56;//exit loop
				}
			}

			if(i==3)
			{
				UART_write("Error : Command Not Found!\r\n");
			}
		}
		free(array[0]);
	}
	return 0;
}
alt_8 UART_write(alt_8* str)
{
	alt_8 len = strlen(str);
	write(uart_file,str,len);
	return 1;//worked
}

alt_8 list_directory(char no_args, char* arg_strings[])
{
	if(no_args==0)
		putty_ls();
	else
		UART_write("ls only works on current dir\r\n");
}

alt_8 change_directory(char no_args, char* arg_strings[])
{
	if(no_args==1)
		if(putty_cd(arg_strings[0])!=0)
		{
			UART_write("Cannot cd to that directory, does it exist?\r\n");
		}
}

alt_8 play_file(char no_args, char* arg_strings[])
{
	//first we need to open the file
	if(no_args!=1)
	{
		UART_write("This needs a file name\r\n");
		return -1;
	}
	euint8 FFound=0;
	SDfile File[200];
	SDfolder Folder[200];
	read_directory(File,Folder,currDir);
	euint16 i=0;
	for(i=0;i<20;i++)
	{
		if(File[i].valid==1)
		{
			euint8 fname[15];
			euint8 x=0,y=0;
			for(x=0;x<8;x++)
			{
				if(File[i].name[x]!=0x20 && File[i].name[x]!=0x00)
				{//if char is not null and is not space
					fname[y] = File[i].name[x];
					++y;
				}
				else
					x=12;
			}
			fname[y]='.';
			++y;
			for(x=0;x<3;x++)
			{
				if(File[i].extension[x]!=0x20 && File[i].extension[x]!=0x00)
				{//if char is not null and is not space
					fname[y] = File[i].extension[x];
					++y;
				}
				else
					x=12;
			}
			//file is a valid file
			fname[y]=0;//add null terminator
			if(strcompare2(fname,arg_strings[0])==0)
			{
				FFound=1;
				open_wav(File[i]);
			}
		}
	}
	/*
	euint8 buffer[512];
	read_file_chunk(File[6],buffer,512,0);
	*/
	if(FFound==0)
	{
		UART_write("That File cannot be found, check spelling and try again\r\n");
	}
	FFound=0;
}

euint8 open_wav(SDfile fileinfo)
{
	euint8 buff[600];
	euint32 buff2[512];
	euint32 right[512];
	euint32 left[512];
	euint8 leftovers[512];
	euint8 leftoverCount=0;
	euint32 count = 0;
	euint32 i,zz;
	euint32 tempright;
	euint32 temp[5];
	euint32 overflow_amount = 0;
	euint32 LoopC = 0;
	euint32 stuff_to_write = 0;
	euint32 ints_to_send = 0;
	euint32 sample_rate = 0;
	euint32 bitdepth = 0;
	euint32 channels = 0;
	euint32 formatcode = 0;
	euint32 blocksize = 0;
	euint32 header_size = 0;
	euint32 next_address = 0;
	euint32 clust_address = 0;
	euint32 FAT_address = 0;
	euint32 filesize = 0;
	euint32 end_offset = 0;
	euint32 bytes_read = 0;
	euint8 end_of_file = 0;

	alt_up_audio_dev*  audio_dev = alt_up_audio_open_dev(AUDIO_NAME);
	alt_up_audio_reset_audio_core(audio_dev);

	filesize = fileinfo.file_size;

	FAT_address = (euint32)fileinfo.startL | ((euint32)fileinfo.startH)<<16;

	clust_address = (FAT_address-2)*Part_Boot.sec_per_clust + clust_start;

	sd_readSector(clust_address,buff);		//read in wav header info
	bytes_read += 512;

	//address = next_sect_address(address);

	header_size = get_wav_header(buff,&sample_rate,&bitdepth,&channels,&formatcode,&blocksize); //psuedo code for reading in wav details, returns wav info

	if(formatcode != 1)
	{
		UART_write("Error: incorrect wav format type, please use PCM\n\r");
		return(1);
	}
	if(channels != 2)
	{
		UART_write("Error, number of channels must be 2\n\r");
		return(1);
	}
	if((bitdepth != 32)&&(bitdepth != 16)&&(bitdepth != 24)&&(bitdepth != 8))
	{
		UART_write("Error, bitdepth is incorrect\n\r");
		return(1);
	}
	if(sample_rate != 8000)
		{
			UART_write("Error, sample rate is incorrect\n\r");
			return(1);
		}
	switch(sample_rate)
		{
		  case 8000:  AUDIO_SetSampleRate(RATE_ADC8K_DAC8K_USB); break;
		  case 32000: AUDIO_SetSampleRate(RATE_ADC32K_DAC32K_USB); break;
		  case 44100: AUDIO_SetSampleRate(RATE_ADC44K_DAC44K_USB); break;
		  case 48000: AUDIO_SetSampleRate(RATE_ADC48K_DAC48K_USB); break;
		  case 96000: AUDIO_SetSampleRate(RATE_ADC96K_DAC96K_USB); break;
		  default:  printf("Non-standard sampling rate\n"); return -1;
	   }
	count = count + header_size;
	while(end_of_file == 0)	//start reading bytes from offset position, if  there are bytes to be read, continue
	{
		//convert char buffer into 32 bit buffer
		while(LoopC < Part_Boot.sec_per_clust)
		{
			if(bitdepth == 32)
			{
				for(i=0;i<128;i++)
				{
					*((unsigned char*)&buff2[i]+0) = buff[count+0];
					*((unsigned char*)&buff2[i]+1) = buff[count+1];
					*((unsigned char*)&buff2[i]+2) = buff[count+2];
					*((unsigned char*)&buff2[i]+3) = buff[count+3];
					count += 4;
					if(count > 512)
					{
						ints_to_send = i+1;
						break;
					}
					if(i==127) ints_to_send = i+1;
				}
				count = 0;
			}
			if(bitdepth == 24)
			{
				for(i=0;i<170;i++)		// PROBLEM HERE PROBS SINCE 512/3 ISNT WHOLE NUMBER
				{
					*((unsigned char*)&buff2[i]+0) = 0;
					*((unsigned char*)&buff2[i]+1) = buff[count+0];
					*((unsigned char*)&buff2[i]+2) = buff[count+1];
					*((unsigned char*)&buff2[i]+3) = buff[count+2];
					count += 3;
					if(count >= 512)
					{
						count = (count-512)+3;
						ints_to_send = i+1;
						break;
					}
				}
			}
			if(bitdepth == 16)
			{
				for(i=0;i<256;i++)
				{
					*((unsigned char*)&buff2[i]+0) = 0;
					*((unsigned char*)&buff2[i]+1) = 0;
					*((unsigned char*)&buff2[i]+2) = buff[count+0];
					*((unsigned char*)&buff2[i]+3) = buff[count+1];
					count += 2;

					if(count > 512)
					{
						ints_to_send = i+1;
						break;
					}
					if(i==255) ints_to_send = i+1;
				}
				count = 0;
			}
			if(bitdepth == 8)
			{
				for(i=0;i<512;i++)
				{
					*((unsigned char*)&buff2[i]+0) = 0;
					*((unsigned char*)&buff2[i]+1) = 0;
					*((unsigned char*)&buff2[i]+2) = 0;
					*((unsigned char*)&buff2[i]+3) = buff[count+0];
					count++;

					if(count > 512)
					{
						ints_to_send = i+1;
						break;
					}
					if(i==511) ints_to_send = i+1;
				}
				count = 0;
			}
			// pass to left and right channels
			for(i=0;i<256;i++)
			{
				left[i] = buff2[i*2];
				right[i] = buff2[i*2+1];
			}

			if(end_offset != 0)
			{
				stuff_to_write = end_offset/(1024/ints_to_send);
				end_of_file = 1;
				end_offset = 0;
			}
			else
			{
				stuff_to_write = ints_to_send/2;
			}
			//is there space in the fifo?
			while(alt_up_audio_write_fifo_space(audio_dev,0) < stuff_to_write);
			//send to audio channels
			alt_up_audio_write_fifo(audio_dev,right,stuff_to_write,ALT_UP_AUDIO_RIGHT);
			alt_up_audio_write_fifo(audio_dev,left,stuff_to_write,ALT_UP_AUDIO_LEFT);
			clust_address++;
			sd_readSector(clust_address,buff);
			bytes_read += 512;
			LoopC++;
			if(bytes_read >= filesize)
			{
				end_of_file = 1;
				//end_offset = 512 - (bytes_read-filesize);
			}
		}
		LoopC = 0;
		clust_address = next_sect_address(FAT_address,&next_address);
		FAT_address = next_address;
		if(bytes_read >= filesize)
		{
			end_of_file = 1;
			//end_offset = 512 - (bytes_read-filesize);
		}
	}
	return(0);
}

int audio_initialise(void)
{
    if(!AUDIO_Init())
    {
    	printf("Unable to initialize audio codec\n");
    	return -1;
    }
    return(0);
}

//returns the address stored in FAT table in the address'th entry of the FAT table
euint32 next_sect_address(euint32 address,euint32* next_FAT_address)
{
	euint8 buffer[512];
	euint32 FATnumber = (address*4)/512;
	euint32 FAT_address;
	euint32 relative_address;
	euint32 next_address;
	euint8 i;
	
	FAT_address = FAT_start + FATnumber;
	relative_address = (address*4 + FAT_start*512) - FAT_address*512;
	relative_address = (address*4)%512;
	sd_readSector(FAT_address,buffer);
	next_address = 	(euint32)buffer[relative_address]
	             | 	((euint32)buffer[relative_address+1])<<8
	             |	((euint32)buffer[relative_address+2])<<16
	             |	((euint32)buffer[relative_address+3])<<24;
	*next_FAT_address = next_address;
	next_address = (next_address -2)*Part_Boot.sec_per_clust + clust_start;
	return(next_address);
}

euint32 get_wav_header(char* buffer, euint32* sample_rate, euint32* bitdepth,  euint32* channels, euint32* formatcode, euint32* blocksize)
{
    typedef struct Riff
    {
        char ChunkID[5];
        euint32 ChunkSize;
        char WaveID[5];
    } RIFF;

    typedef struct Wave
    {
        char ckID[5];
        euint32 cksize;
        euint32 FormatCode;
        euint32 Channels;
        euint32 SamplesPerSec;
        euint32 BytesPerSec;
        euint32 BlockAlign;
        euint32 BitsPerSample;
        euint32 Size;
        euint32 ValidBitsPerSample;
    } Wav;

    typedef struct Data
    {
      char ckID[5];
      euint32 cksize;
    } DATA;

    euint32 i;
    euint8 header[512];

    for(i=0;i<512;i++)
    {
    	header[i] = buffer[i];
    }

    RIFF riff;
    Wav wave;
    DATA data;

    for(i=0;i<4;i++)
    {
       riff.ChunkID[i] = header[i];
       riff.WaveID[i] = header[i+8];
       wave.ckID[i] = header[i+12];
       data.ckID[i] = header[i+36];
    }
    riff.ChunkID[4] = 0x0;
    riff.WaveID[4] = 0x0;
    wave.ckID[4] = 0x0;
    data.ckID[4] = 0x0;

    riff.ChunkSize = extract_little(header,4,4);
    wave.cksize = extract_little(header,16,4);
    wave.FormatCode = extract_little(header,20,2);
    wave.Channels = extract_little(header,22,2);
    wave.SamplesPerSec = extract_little(header,24,4);
    wave.BytesPerSec = extract_little(header,28,4);
    wave.BlockAlign = extract_little(header,32,2);
    wave.BitsPerSample = extract_little(header,34,2);
    data.cksize = extract_little(header,40,4);
    //euint32 header_size = riff.ChunkSize - data.cksize + 8; //size of the header portion
    euint32 header_size = 44;
    *sample_rate = wave.SamplesPerSec;
	*bitdepth = wave.BitsPerSample;
	*channels = wave.Channels;
	*formatcode = wave.FormatCode;
	*blocksize = wave.BlockAlign;

    return (header_size);
}

euint8 putty_ls()
{
	SDfile File[20];
	SDfolder Folder[20];
	read_directory(File,Folder,currDir);
	ls(File,Folder);
	return 0;
}

euint8 putty_cd(euint8* path)
{
	SDfolder original;original = currDir;/*Backup*/
	if(path[0]=='/')
	{
		//absolute address
		currDir = root;//move back to the root folder then we can begin
		strChomp(path);
	}
	else
	{
		//relative address
	}
	/*new we pass through the strings section by section attempting to move*/

	printf("-->%s\n",path);
	char* tokens[20];
   	euint8 count = string_parser2(path,tokens);
   	euint8 i=0;
   	euint8 abort=0;
   	for(i=0;i<count;i++)
   	{
	   printf("->%s\n", tokens[i]);
	   if(abort==0)
	   {
			if(cd( tokens[i])!=0)
			{
			   abort=1;
			}
			free(tokens[i]);
   	   }
   if(abort==1)
   {
	   currDir = original;
	   return -1;
   }
   else
		return 0;
}
/*Removes the first char from a string*/
void strChomp(euint8* s)
{
	euint32 i=0;
	for(i=1;i<strlen(s);i++)
	{
		s[i-1]=s[i];
	}
	s[i-1]=0;//move the null
}

euint32 extract_little(char* str, euint32 ofset, euint32 n)
{
    if((n>sizeof(euint32)) || (n<1))
    {
        printf("Incorrect number of bytes for type integer\n");
        return(-1);
    }
    if (str == 0)
    {
        printf("Invalid address given\n");
        return(-1);
    }

    euint32 little_endian = 0;
    euint32 i;

    for (i=0;i<n;i++)
    {
        *((unsigned char *)&little_endian + i) = *(str + ofset + i);
    }
    return(little_endian);
}

euint32 extract_big(char* str, euint32 ofset, euint32 n)
{
      if(n>sizeof(euint32))
    {
        printf("Number of bytes exceeds integer length\n");
        return(-1);
    }
    if (str == 0)
    {
        printf("Invalid address given\n");
        return(-1);
    }

    euint32 big_endian = 0;
    euint32 i;

    for (i=0;i<n;i++)
    {
        *((unsigned char *)&big_endian + (n-i-1)) = *(str + ofset + i);
    }
    return(big_endian);
}

char string_parser2(char* inp, char* array_of_words[])
{
    euint32 i = 0;
    euint32 j = 0;
    euint32 k = 0;
    char temp[500];

    if(inp == 0)
    {
        printf("Invalid address given\n");
        return(0);
    }

    if(inp[0] == 0x0)
    {
        printf("String contains no words\n");
        return(0);
    }

    while(inp[i] != 0x0)
    {
        while((inp[i] != '/') && (inp[i] != 0x0))
        {
        	if(k==500)
        	{
        		printf("Error: Word parsing word greater than buffer length");
        		break;

        	}
            temp[k] = inp[i];
            i++;
            k++;
        }
        while(inp[i] == '/')
        {
            i++;
        }
        temp[k] = 0x0;
        array_of_words[j] = malloc(k);
        strcpy(array_of_words[j], temp);
        k = 0;
        j++;
    }
    return(j);
}

euint8 cd(euint8* FolderName)
{
	SDfile File2[200];
	SDfolder Folder2[200];
	if(FolderName == "." )
		return 0;
	if(FolderName=="..")
	{
		return -1;
	}
	read_directory(File2,Folder2,currDir);
	euint8* search = (euint8*)malloc(strlen(FolderName) - LastIndexOf('/',FolderName));
	strCopyLen(FolderName,search,LastIndexOf('/',FolderName));
	euint16 counter=0;
	while(!(Folder2[counter].startH ==0 && Folder2[counter].startL==0)&&counter<20)
	{
		if(strcompare2(Folder2[counter].name,search)==0)
		{
			//we have found our folder
			//now we need to update our pointers
			currDir = Folder2[counter];
			//free(search);
			return 0;
		}
		++counter;
	}
	free(search);
	return -1;
}

euint32 trim(char* buf)
{
	euint32 i=0;
	for(i=strlen(buf);i>=0;i--)
	{
		if(buf[i]!=0x20 && buf[i]!=0x00)
			return 0;
		else
		{
			buf[i]=0;
		}
	}
	return 0;
}

euint32 strcompare(char* buf1,char* buf2)
{
	euint32 index=0;
	while(buf1[index] != 0 ||buf2[index] != 0  )
	{
		if( buf2[index]== 0|| buf1[index]== 0)
		{//ended pre-maturely
			return -1;
		}
	if(buf1[index]!= buf2[index])return -1;
	index++;
	}
	return 0;
}

/*Compare two strings if they are equal or not*/
euint32 strcompare2(char* buf1,char* buf2)
{
	trim(buf1);
	trim(buf2);
	euint32 index=0;
	while(buf1[index] != 0 ||buf2[index] != 0  )
	{
		if(buf1[index]!= buf2[index])
		{
			return -1;
		}
		if(( buf2[index]== 0|| buf1[index]== 0))
		{ //ended pre-maturely
			return -1;
		}
	index++;
	}
	return 0;
}

euint8 strCopyLen(euint8* s,euint8* d,euint16 offset)
{
	euint32 i=0;
	for(i=offset;i<strlen(s);i++)
	{
		d[i-offset] = s[i];//copy the char
	}
	d[i-offset]=0;
	return i-offset;//return the number of bytes copied
}

euint16 LastIndexOf(char c,euint8* s)
{
	euint32 i = strlen(s)-1;
	if(i>0)
	{
		for(;i>=0;i--)
		{
			if(i==0) return 0;
			if(s[i] == c)
				return i;
		}
	}
	return 0;
}

euint8 ls(SDfile* File,SDfolder* Folder)
{
	euint16 counter=0;
	euint8 empty=0;
	char buffer[64];
	for(counter=0;counter<20;counter++)
		if((File[counter].valid==1))
			{
				printf("%s.%s\t%dKB\n",File[counter].name,File[counter].extension,(File[counter].file_size/1024));
				sprintf(buffer,"%s.%s\t%dKB\r\n",File[counter].name,File[counter].extension,(File[counter].file_size/1024));
				UART_write(buffer);
				empty=1;
			}
	counter=0;
	for(counter=0;counter<20;counter++)
		if((Folder[counter].valid==1))
			{
				printf("%s\t<DIR>\n",Folder[counter].name);
				sprintf(buffer,"%s\t<DIR>\r\n",Folder[counter].name);
				UART_write(buffer);
				empty=1;
			}
	if(empty==0)
	{
		printf("Empty Folder\n");
		sprintf(buffer,"Empty Folder\r\n");
		UART_write(buffer);
	}
return 0;
}

euint8 read_rootTable(euint32 LBABegin,euint32 ResSect)
{
	euint8 buffer[size];
	sd_readSector(LBABegin+ResSect+3,buffer);

	printf("\n");
	return 0;
}

euint8 SD_init(void)
{
	if (if_initInterface() != 0)
	{
		printf("SD failed to initialise\n");
		return(1);
	}
	return(0);
}

euint8 read_mboot(euint8* buffer)
{
	if(sd_readSector(mboot,buffer) != 0)
	{
		printf("failed sector read\n");
		return(-1);
	}
	return(0);
}

euint8 read_part_boot()
{
	euint8 buffer[size];
	if(sd_readSector(LBAbegin,buffer) != 0)
	{
		printf("failed sector read\n");
		return(-1);
	}
	Part_Boot.bytes_per_sec = (buffer[11]|((euint16) buffer[12])<<8);
	Part_Boot.sec_per_clust = (euint8)buffer[13];
	Part_Boot.res_sec = (buffer[14]|((euint16) buffer[15])<<8);
	Part_Boot.FAT_num = (euint8)buffer[16];
	Part_Boot.sec_num_small =(buffer[19]|((euint16) buffer[20])<<8);
	Part_Boot.sec_num_big = ((euint32)buffer[32]|((euint32) buffer[33])<<8|((euint32) buffer[34])<<16|((euint32) buffer[35])<<24);
	Part_Boot.sec_per_FAT = ((euint32)buffer[36]|((euint32) buffer[37])<<8|((euint32) buffer[38])<<16|((euint32) buffer[39])<<24);
	Part_Boot.clust_num = ((euint32)buffer[44]|((euint32) buffer[45])<<8|((euint32) buffer[46])<<16|((euint32) buffer[47])<<24);
	return(0);
}

euint32 read_file_chain_length(SDfile fileInfo)
{
	euint32 current_address = (fileInfo.startL | (euint32)fileInfo.startH<<16);
	euint32 FAT_start = LBAbegin + Part_Boot.res_sec;
	euint8 buffer1[size];
	euint32 length = 0;
	euint32 modulus = 0;
	while(current_address < 0x0FFFFFF8)
		{
			//NOTE 0x0FFFFFF7 means the cluster is bad so we may need to skip
			euint32 FATNumber = (current_address*4)/512;// will floor for us
			// Read in the new sector
			sd_readSector(FAT_start+FATNumber,buffer1);	//reads FAT
			// Find the offset from start of FATNumber as per the modulus
			modulus = (current_address/4)%512;
			// Increment now to try and catch the last entry as well
			length++;
			// Get next address
			current_address = (euint32)buffer1[current_address*4+3] << 24 |
					(euint32)buffer1[current_address*4+2] << 16 |
					(euint32)buffer1[current_address*4+1] << 8 |
					(euint32)buffer1[current_address*4];
		}
	// vvv For debugging only vvv
	printf("Length of Chain: %d\r\n", length);
	return length;
}

}
euint8 read_directory(SDfile* File,SDfolder* Folders,SDfolder CurrentFolder)
{
	euint8 buffer1[size];
	euint8 buffer2[size];
	euint8 LoopC =0;
	euint32 current_address = (CurrentFolder.startL | (euint32)CurrentFolder.startH<<16);
	euint32 clust_read_pos;
	euint32 fileCount=0,folderCount=0;
	FAT_start = LBAbegin + Part_Boot.res_sec;
	clust_start = 2*Part_Boot.sec_per_FAT + FAT_start;
	euint32 y=0;
	for(y=0;y<20;y++)
	{
		File[y].startH=0;
		File[y].startL=0;
		File[y].valid=0;
		Folders[y].startH=0;
		Folders[y].startL=0;
		Folders[y].valid=0;
	}

	while(current_address < 0x0FFFFFF8)
	{
		while(LoopC<Part_Boot.sec_per_clust)
		{
			//NOTE 0x0FFFFFF7 means the cluster is bad so we may need to skip
			euint32 FATNumber = (current_address*4)/512;// will floor for us

			sd_readSector(FAT_start+FATNumber,buffer1);	//reads FAT

			//location of current cluster to read
			clust_read_pos = (current_address-2)*Part_Boot.sec_per_clust + clust_start + LoopC;

			//read the current cluster
			sd_readSector(clust_read_pos,buffer2);

			//do shit to cluster
			uint i,b;
			for(i=0;i<16;i++)
			{
				euint32 addBase = i*32;
				euint32 offset =0;
				if(buffer2[addBase+0x0b]!=0x0F){
				if(buffer2[addBase] !=0 && buffer2[addBase] != 0xe5&& buffer2[addBase] != 0x2e)
				{
					if(buffer2[addBase+11] & 0x10)
					{
						//directory
						/*Parsing the data from the bytes into the stuct*/
						if(buffer2[addBase+0x0b]!=0x0F)
						{
							if( buffer2[addBase] ==0x05)
								Folders[folderCount].name[0] = 0xe5;
							else
								Folders[folderCount].name[0] = buffer2[addBase];
							for(b=1;b<8;b++)
							{
								Folders[folderCount].name[b] = buffer2[addBase+b];

							}
							Folders[folderCount].name[8]=0;
							offset+=8;
							for(b=0;b<3;b++)
							{
								Folders[folderCount].extension[b] = buffer2[addBase+b+offset];
							}
							offset+=3;
							Folders[folderCount].extension[3]=0;
							offset=0x14;
							Folders[folderCount].startH = (euint16)buffer2[addBase+offset] | ((euint16)(buffer2[addBase+offset+1]))<<8;
							offset=0x1A;
							Folders[folderCount].startL = (euint16)buffer2[addBase+offset] | ((euint16)(buffer2[addBase+offset+1]))<<8;
								/*Store the parents folders address in the children's memory for future use*/
							euint32 x=0;
							x = CurrentFolder.startL | (euint32)CurrentFolder.startH<<16;
							Folders[folderCount].ParentAdd=x;
							Folders[folderCount].valid=1;

							//printf("FLDR : %s-%s\r\n",Folders[folderCount].name,Folders[folderCount].extension);
							++folderCount;
						}
					}
					else
					{
						if(buffer2[addBase+0x0b]!=0x0F)
						{
						/*Account for the off movements*/
						if( buffer2[addBase] ==0x05)
							File[fileCount].name[0] = 0xe5;
						else
							File[fileCount].name[0] = buffer2[addBase];
						for(b=1;b<8;b++)
							{
								File[fileCount].name[b] = buffer2[addBase+b];
							}
						offset+=8;
						File[fileCount].name[8]=0;
						for(b=0;b<3;b++)
							{
								File[fileCount].extension[b] = buffer2[addBase+b+offset];
							}
						offset+=3;
						File[fileCount].extension[3]=0;
						printf("FILE : %s.%s\r\n",File[fileCount].name,File[fileCount].extension);
						File[fileCount].attributes = buffer2[addBase+offset];
						offset=0x14;
						File[fileCount].startH = buffer2[addBase+offset] | (euint16)buffer2[addBase+offset+1]<<8;
						offset=0x1A;
						File[fileCount].startL = buffer2[addBase+offset] | (euint16)buffer2[addBase+offset+1]<<8;
						offset=0x1C;
						File[fileCount].file_size = buffer2[addBase+offset] | (euint32)buffer2[addBase+offset+1]<<8 | (euint32) buffer2[addBase+offset+2]<<16 | (euint32) buffer2[addBase+offset+3]<<24;
						File[fileCount].valid=1;
						++fileCount;//increment
						}
					}
				}
				else
					{
					if(buffer2[addBase] == 0x2e )
					{
						/*if(buffer2[addBase+1]='.')
						{
							euint32 offset=0x14;
							euint32 x =0;
							x =(euint16)buffer2[addBase+offset] | ((euint16)(buffer2[addBase+offset+1]))<<8;
							x =x <<16;
							offset=0x1A;
							x|= (euint16)buffer2[addBase+offset] | ((euint16)(buffer2[addBase+offset+1]))<<8;
							CurrentFolder.ParentAdd=x;
						}*/
					}
					if(buffer2[addBase] == 0)
					{
					//we are done and at end of directory chain
						return 1;
					}
							//end of directory or file deleted
					}
				}
			}

			//get next FAT address

			//current_address = current_address + 512;
			++LoopC;
		}

		LoopC =0;
		current_address = buffer1[((current_address*4)%512)] | (euint32)buffer1[((current_address*4)%512)+1]<<8 | (euint32) buffer1[((current_address*4)%512)+2]<<16 | (euint32) buffer1[((current_address*4)%512)+3]<<24;
	}
	return(0);
}

void UARTListener(char buffer[],euint32 uart)
{
buffer[0]=0;//make it start with a null for now
	char Input[3];
	euint32 bufferPointer = 0;
	euint32 Run = 1;
	while (Run == 1) {
		if (read(uart, Input, 1) == 1) {
			if (bufferPointer < 0)
				bufferPointer = 0;
			if (Input[0] > 0) {
				if (Input[0] == '\r' || Input[0] == '\n') {
					//Then the user has pressed enter, load the buffer into the processor
					Input[0] = '\r';
					Input[1] = '\n';
					write(uart, Input, 2);
					buffer[bufferPointer] = 0;//add null
					printf(Input, 2);

					return ;
					/*CommandProcessor(buffer, bufferPointer);
					printf(Input, 2);
					bufferPointer = 0;
					Input[0] = 0;
					Input[1] = 0;*/
				} else if (Input[0] == 0x7F) {
					//7F== backspace
					write(uart, Input, 1);//send it back out
					--bufferPointer;
					buffer[bufferPointer] = 0;
				} else {
					write(uart, Input, 1);//send it back out
					buffer[bufferPointer] = Input[0];
					bufferPointer++;
				}
			}

		}
	}

}

char string_parser(char* inp, char* array_of_words[])
{
    euint32 i = 0;
    euint32 j = 0;
    euint32 k = 0;
    char temp[500];

    if(inp == 0)
    {
        printf("Invalid address given\n");
        return(0);
    }

    if(inp[0] == 0x0)
    {
        printf("String contains no words\n");
        return(0);
    }

    while(inp[i] != 0x0)
    {
        while((inp[i] != 0x20) && (inp[i] != 0x0))
        {
        	if(k==500)
        	{
        		printf("Error: Word parsing word greater than buffer length");
        		break;

        	}
            temp[k] = inp[i];
            i++;
            k++;
        }
        while(inp[i] == 0x20)
        {
            i++;
        }
        temp[k] = 0x0;
        array_of_words[j] = malloc(k);
        strcpy(array_of_words[j], temp);
        k = 0;
        j++;
    }
    return(j);
}
