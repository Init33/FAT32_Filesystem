#ifndef FAT_H
#define FAT_H

#define mboot 0
#define size 512

typedef struct
{
	euint16 bytes_per_sec;
	euint8 sec_per_clust;
	euint16 res_sec;
	euint8 FAT_num;
	euint16 sec_num_small;
	euint32 sec_num_big;
	euint32 sec_per_FAT;
	euint32 clust_num;
}part_boot;

typedef struct
{
	euint8 name[9];
	euint8 extension[4];
	euint8 attributes;
	euint16 startH;
	euint16 startL;
	euint32 file_size;
	euint8 valid;
}SDfile;

typedef struct
{
	euint8 name[9];
	euint8 extension[4];
	euint8 attributes;
	euint16 startH;
	euint16 startL;
	euint32 ParentAdd;
	euint8 valid;
}SDfolder;

typedef struct
{
	euint32 *DataBlocks;
	euint32 noDataBlocks;
}SDfileContents;

// structure for commands
typedef struct
{
	char* com_string;
	alt_32 (*com_fun)(char no_args, char* arg_strings[]);
} command;


euint8 SD_init(void);
euint8 read_mboot(euint8* buffer);
euint8 read_part_boot();
euint8 read_rootTable(euint32 LBABegin,euint32 ResSect);
euint8 read_directory(SDfile* File,SDfolder* Folders,SDfolder CurrentFolder);
euint8 ls(SDfile* File,SDfolder* Folders);
euint8 strCopyLen(euint8* s,euint8* d,euint16 offset);
euint16 LastIndexOf(char c,euint8* s);
euint8 cd(euint8* FolderName);
euint32 strcompare2(char* buf1,char* buf2);
char string_parser2(char* inp, char* array_of_words[]);
euint32 trim(char* buf);
euint8 putty_ls();
void strChomp(euint8* s);
euint32 read_file_chain_length(SDfile fileInfo);
euint8 putty_cd(euint8* path);
euint8 read_file_chunk(SDfile fileInfo,euint8* buffer,euint32 bufferLength,euint32 bufferNo);
alt_8 UART_write(alt_8* str);
alt_8 list_directory(char no_args, char* arg_strings[]);
alt_8 change_directory(char no_args, char* arg_strings[]);
alt_8 play_file(char no_args, char* arg_strings[]);

euint8 open_wav(SDfile fileinfo);
euint32 get_wav_header(char* buffer, euint32* sample_rate, euint32* bitdepth,  euint32* channels, euint32* formatcode, euint32* blocksize);
euint32 extract_big(char* str, euint32 ofset, euint32 n);
euint32 extract_little(char* str, euint32 ofset, euint32 n);
euint32 next_sect_address(euint32 address,euint32* next_FAT_address);

int audio_initialise(void);
void UARTListener(char buffer[],euint32 uart);
euint32 strcompare(char* buf1,char* buf2);
char string_parser(char* inp, char* array_of_words[]);

#endif
