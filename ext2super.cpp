/**
 * Descrição: Este programa implementa comandos sobre o sistema de arquivos EXT2.
 *
 * Autor: Christofer Daniel Rodrigues Santos, Guilherme Augusto Rodrigues Maturana, Renan Guensuke Aoki Sakashita
 * Data de criação: 22/10/2022
 * Datas de atualização: 04/11/2022, 11/11/2022, 18/11/2022, 25/11/2022, 02/12/2022
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/types.h>
#include "ext2.h"
#include <string.h>
#include <time.h>  

// Shell

#include <sys/wait.h>
#include <sys/resource.h>
#include <readline/readline.h>
#include <readline/history.h>

// Pilha

#include <iostream>
#include <stack>
using namespace std;

#define BASE_OFFSET 1024											 // Localização do superbloco
#define FD_DEVICE "./myext2image.img"								 // Imagem do sistema de arquivos
#define EXT2_SUPER_MAGIC 0xEF53										 // Número mágico do EXT2
#define BLOCK_OFFSET(block) (BASE_OFFSET + (block - 1) * block_size) // Função que calcula a posição de um bloco com base em seu número
#define block_size (1024 << super.s_log_block_size)					 // Tamanho do bloco: s_log_block_size expressa o tamanho do bloco em potências de 2
																	 // Como temos que s_log_block_size = 0, temos que o tamanho do bloco é dado por 1024 * 2^0 = 1024

#define EXT2_S_IRUSR	0x0100	// user read
#define EXT2_S_IWUSR	0x0080	// user write
#define EXT2_S_IXUSR	0x0040	// user execute
#define EXT2_S_IRGRP	0x0020	// group read
#define EXT2_S_IWGRP	0x0010	// group write
#define EXT2_S_IXGRP	0x0008	// group execute
#define EXT2_S_IROTH	0x0004	// others read
#define EXT2_S_IWOTH	0x0002	// others write
#define EXT2_S_IXOTH	0x0001	// others execute

static struct ext2_super_block super;
static int fd;
// stack<ext2_inode> currentPath;
stack<string> currentPath;
int grupoAtual = 0;

// Posiciona o leitor do arquivo em  (Inicio_Tabela_Inodes + Distancia_Inode_Desejado) bytes e lê o Inode desejado na variável Inode passada por parâmetro
static void read_inode(unsigned int inode_no, struct ext2_group_desc *group, struct ext2_inode *inode)
{
	lseek(fd, BLOCK_OFFSET(group->bg_inode_table) + (inode_no - 1) * sizeof(struct ext2_inode),
		  SEEK_SET);
	read(fd, inode, sizeof(struct ext2_inode));
}

/* Se o grupo do Inode é diferente do grupo atual: atualiza a variável grupoAtual e posiciona o leitor do arquivo no descritor do novo grupo, fazendo a leitura
deste na variável group passada por parâmetro

valor: valor do Inode
grupoAtual: Grupo corrente
*/
void trocaGrupo(unsigned int *valor, struct ext2_group_desc *group, int *grupoAtual)
{
	unsigned int block_group = ((*valor) - 1) / super.s_inodes_per_group; // Cálculo do grupo do Inode

	if (block_group != (*grupoAtual))
	{
		*grupoAtual = block_group;

		lseek(fd, BASE_OFFSET + block_size + sizeof(struct ext2_group_desc) * block_group, SEEK_SET);
		read(fd, group, sizeof(struct ext2_group_desc));
	}
}

/* Atualiza valorInode com o Inode da entrada que possui nome 'nome'

inode, group: Inode/Grupo do diretório
valorInode: variável que receberá o Inode da entrada com nome 'nome'
nome: nome da entrada procurada no diretório
 */
void read_dir(struct ext2_inode *inode, struct ext2_group_desc *group, unsigned int *valorInode, char *nome)
{
	void *block;
	(*valorInode) = -1;
	if (S_ISDIR(inode->i_mode))
	{
		struct ext2_dir_entry_2 *entry;
		unsigned int size = 0;

		if ((block = malloc(block_size)) == NULL)
		{ /* allocate memory for the data block */
			fprintf(stderr, "Memory error\n");
			close(fd);
			exit(1);
		}

		lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
		read(fd, block, block_size); /* read block from disk*/

		entry = (struct ext2_dir_entry_2 *)block; /* first entry in the directory */
												  /* Notice that the list may be terminated with a NULL
													 entry (entry->inode == NULL)*/

		while ((size < inode->i_size) && entry->inode)
		{
			char file_name[EXT2_NAME_LEN + 1];
			memcpy(file_name, entry->name, entry->name_len);
			file_name[entry->name_len] = 0; /* append null character to the file name */
			// printf("%10u %s\n", entry->inode, file_name);
			if (!strcmp(nome, file_name))
			{
				*valorInode = entry->inode;
				break;
			}
			// printf("%s", nometmp);
			entry = (ext2_dir_entry_2 *)((char *)entry + entry->rec_len);
			size += entry->rec_len;
		}

		free(block);
	}
}

// Exibe as informações do Inode passado por parâmetro
void printInode(struct ext2_inode *inode)
{
	printf("Reading Inode\n"
		   "File mode: %hu\n"
		   "Owner UID: %hu\n"
		   "Size     : %u bytes\n"
		   "Blocks   : %u\n",
		   inode->i_mode,
		   inode->i_uid,
		   inode->i_size,
		   inode->i_blocks);

	for (int i = 0; i < EXT2_N_BLOCKS; i++)
		if (i < EXT2_NDIR_BLOCKS) /* direct blocks */
			printf("Block %2u : %u\n", i, inode->i_block[i]);
		else if (i == EXT2_IND_BLOCK) /* single indirect block */
			printf("Single   : %u\n", inode->i_block[i]);
		else if (i == EXT2_DIND_BLOCK) /* double indirect block */
			printf("Double   : %u\n", inode->i_block[i]);
		else if (i == EXT2_TIND_BLOCK) /* triple indirect block */
			printf("Triple   : %u\n", inode->i_block[i]);
}

// Exibe as informações do arquivo do Inode passado por parâmetro, com tratamento de indireção
void printaArquivo(struct ext2_inode *inode)
{
	char *buffer = (char *)malloc(sizeof(char) * block_size);

	lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET); // Posiciona o leitor no primeiro bloco de dados do Inode
	read(fd, buffer, block_size);						  // Lê este bloco em buffer

	int sizeTemp = inode->i_size;

	// Com blocos de tamanho de 1024 bytes, haverão 256 blocos diretos em blocos de uma indireção e 256 blocos indiretos em blocos de dupla indireção
	int singleInd[256];
	int doubleInd[256];

	for (int i = 0; i < 12; i++) // Itera sobre os blocos de dados sem indireção
	{
		lseek(fd, BLOCK_OFFSET(inode->i_block[i]), SEEK_SET);
		read(fd, buffer, block_size); // Lê bloco i em buffer

		for (int i = 0; i < 1024; i++) // Exibe o conteúdo do bloco i
		{
			printf("%c", buffer[i]);

			sizeTemp = sizeTemp - sizeof(char); // Controle de dados restantes

			if (sizeTemp <= 0)
			{
				break;
			}
		}
		if (sizeTemp <= 0)
		{
			break;
		}
	}

	if (sizeTemp > 0) // Se depois dos blocos sem indireção ainda há dados, passa pelo bloco 12 que tem uma indireção
	{
		lseek(fd, BLOCK_OFFSET(inode->i_block[12]), SEEK_SET);
		read(fd, singleInd, block_size);

		for (int i = 0; i < 256; i++) // Iteração sobre os blocos diretos do bloco 12
		{
			lseek(fd, BLOCK_OFFSET(singleInd[i]), SEEK_SET);
			read(fd, buffer, block_size);

			for (int j = 0; j < 1024; j++)
			{
				printf("%c", buffer[j]);
				sizeTemp = sizeTemp - 1;

				if (sizeTemp <= 0)
				{
					break;
				}
			}
			if (sizeTemp <= 0)
			{
				break;
			}
		}
	}

	if (sizeTemp > 0) // Se depois dos blocos com uma indireção ainda há dados, passa pelo bloco 13 que tem dupla indireção
	{
		lseek(fd, BLOCK_OFFSET(inode->i_block[13]), SEEK_SET);
		read(fd, doubleInd, block_size);

		for (int i = 0; i < 256; i++)
		{
			if (sizeTemp <= 0)
			{
				break;
			}

			lseek(fd, BLOCK_OFFSET(doubleInd[i]), SEEK_SET);
			read(fd, singleInd, block_size);

			for (int k = 0; k < 256; k++) // Iteração sobre os blocos diretos do bloco 13
			{
				if (sizeTemp <= 0)
				{
					break;
				}

				lseek(fd, BLOCK_OFFSET(singleInd[k]), SEEK_SET);
				read(fd, buffer, block_size);

				for (int j = 0; j < 1024; j++)
				{
					printf("%c", buffer[j]);
					sizeTemp = sizeTemp - 1;
					if (sizeTemp <= 0)
					{
						break;
					}
				}
			}
		}
	}

	free(buffer);
}

/* void leArquivoPorNome(struct ext2_inode *inode, struct ext2_group_desc *group, char *nome, int *grupoAtual)
{
	printf("\n---Teste leitura de arquivo---\n");
	unsigned int valorInodeTmp = 0;
	char *nomeArquivo = nome;
	struct ext2_group_desc *grupoTemp = (struct ext2_group_desc *) malloc(sizeof(struct ext2_group_desc));
	struct ext2_inode *inodeTemp = (struct ext2_inode *) malloc(sizeof(struct ext2_inode));
	memcpy(grupoTemp, group, sizeof(struct ext2_group_desc));
	memcpy(inodeTemp, inode, sizeof(struct ext2_inode));
	// grupoTemp = group;
	read_dir(inodeTemp, grupoTemp, &valorInodeTmp, nomeArquivo);
	if (valorInodeTmp == -1)
	{
		printf("ARQUIVO NÃO ENCONTRADO");
		return;
	}

	trocaGrupo(&valorInodeTmp, grupoTemp, grupoAtual);
	// printf("TROCOU DE GRUPO");
	read_inode(valorInodeTmp, grupoTemp, inodeTemp);

	printaArquivo(inodeTemp);
	free(grupoTemp);
	free(inodeTemp);
	// printf("INODE: %d\n", valor);

	// printf("TAMANHO: %d\n", inode->i_size);
	// printf("TESTE: %d\n", inode->i_block[0]);
	// char *teste;
	// printf("FD: %d", fd);
	// printf("\n---%d---\n", BLOCK_OFFSET(inode->i_block[0]));
	// read(fd2, teste, block_size);
//	for (int i = 0; i < inode->i_size; i++)
//	{
//		printf("%c", teste[i]);
//	}
}
*/

// Exibe as informações do Grupo passado por parâmetro
void printGroup(struct ext2_group_desc *group)
{
	printf("\n\n\nReading first group-descriptor from device " FD_DEVICE ":\n"
		   "Blocks bitmap block: %u\n"
		   "Inodes bitmap block: %u\n"
		   "Inodes table block : %u\n"
		   "Free blocks count  : %u\n"
		   "Free inodes count  : %u\n"
		   "Directories count  : %u\n",
		   group->bg_block_bitmap,
		   group->bg_inode_bitmap,
		   group->bg_inode_table,
		   group->bg_free_blocks_count,
		   group->bg_free_inodes_count,
		   group->bg_used_dirs_count);
}

// Abre a imagem do sistema de arquivos, lê o Superbloco em super, verifica o número mágico, lê o Grupo 0 em group, e lê o Inode 2 em inode
void init_super(struct ext2_group_desc *group, struct ext2_inode *inode)
{
	// Abre a imagem do sistema de arquivos
	if ((fd = open(FD_DEVICE, O_RDONLY)) < 0)
	{
		perror(FD_DEVICE);
		exit(1);
	}

	// Leitura do Superbloco
	lseek(fd, BASE_OFFSET, SEEK_SET);
	read(fd, &super, sizeof(super));

	// Verificação do número mágico
	if (super.s_magic != EXT2_SUPER_MAGIC)
	{
		fprintf(stderr, "Not a Ext2 filesystem\n");
		exit(1);
	}

	// Leitura do Grupo
	lseek(fd, BASE_OFFSET + block_size, SEEK_SET);
	read(fd, group, sizeof(struct ext2_group_desc));

	// Leitura do Inode
	read_inode(2, group, inode);
}

/* Inicia novoInode e novoGrupo com o Grupo e o Inode do arquivo com nome 'nome'

novoInode, novoGroup: variáveis auxiliares para manutenção dos antigos valores em inode e group
*/
void getArquivoPorNome(struct ext2_inode *inode, struct ext2_group_desc *group, char *nome, int *grupoAtual, struct ext2_inode *novoInode, struct ext2_group_desc *novoGroup)
{
	unsigned int valorInodeTmp;

	// Cópia das variáveis para mudança de valores de Grupo e Inode
	memcpy(novoGroup, group, sizeof(struct ext2_group_desc));
	memcpy(novoInode, inode, sizeof(struct ext2_inode));

	// grupoTemp = group;

	// Atualização do Inode para o que contém o arquivo 'nome'
	read_dir(novoInode, novoGroup, &valorInodeTmp, nome);

	// ERRO: valorInodeTmp é unsigned
	if (valorInodeTmp == -1)
	{
		printf("ARQUIVO NÃO ENCONTRADO");
		return;
	}

	// printf("TROCOU DE GRUPO");

	// Atualização do Groupo para o que contém o novo Inode
	trocaGrupo(&valorInodeTmp, novoGroup, grupoAtual);

	// Cálculo do index real do Inode no novo Grupo
	unsigned int index = valorInodeTmp % super.s_inodes_per_group;

	// Atualização do Inode no novo Grupo
	read_inode(index, novoGroup, novoInode);
}

// Copia o conteúdo dos blocos de dados em inode para arquivo
void copiaArquivo(struct ext2_inode *inode, FILE *arquivo)
{
	// Aloca um bloco
	char *buffer = (char *)malloc(sizeof(char) * block_size);

	// Posiciona o leitor no primeiro bloco de dados diretos e o lê em buffer
	lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
	read(fd, buffer, block_size);

	int sizeTemp = inode->i_size;
	int singleInd[256];
	int doubleInd[256];
	char charLido = '0';

	// Iteração sobre os blocos de dados diretos
	for (int i = 0; i < 12; i++)
	{
		// Leitura do bloco i em buffer
		lseek(fd, BLOCK_OFFSET(inode->i_block[i]), SEEK_SET);
		read(fd, buffer, block_size);

		// Iteração sobre o conteúdo do bloco i
		for (int i = 0; i < 1024; i++)
		{
			// Copia o caracter i para charLido
			charLido = buffer[i];

			// Escreve charLido em arquivo
			fputc(charLido, arquivo);

			// Decrementa sizeTemp que marca quantos bytes do Inode restam para serem lidos
			sizeTemp = sizeTemp - sizeof(char);

			if (sizeTemp <= 0)
			{
				break;
			}
		}
		if (sizeTemp <= 0)
		{
			break;
		}
	}

	// Se há blocos a serem lidos após a leitura dos blocos diretos, lê o bloco 12 com uma indireção
	if (sizeTemp > 0)
	{
		lseek(fd, BLOCK_OFFSET(inode->i_block[12]), SEEK_SET);
		read(fd, singleInd, block_size);

		for (int i = 0; i < 256; i++)
		{
			lseek(fd, BLOCK_OFFSET(singleInd[i]), SEEK_SET);
			read(fd, buffer, block_size);

			for (int j = 0; j < 1024; j++)
			{
				charLido = buffer[j];
				fputc(charLido, arquivo);
				sizeTemp = sizeTemp - 1;
				if (sizeTemp <= 0)
				{
					break;
				}
			}
			if (sizeTemp <= 0)
			{
				break;
			}
		}
	}

	// Se há blocos a serem lidos após a leitura do bloco 12, lê o bloco 13 com dupla indireção
	if (sizeTemp > 0)
	{
		lseek(fd, BLOCK_OFFSET(inode->i_block[13]), SEEK_SET);
		read(fd, doubleInd, block_size);

		for (int i = 0; i < 256; i++)
		{
			if (sizeTemp <= 0)
			{
				break;
			}

			lseek(fd, BLOCK_OFFSET(doubleInd[i]), SEEK_SET);
			read(fd, singleInd, block_size);

			for (int k = 0; k < 256; k++)
			{
				if (sizeTemp <= 0)
				{
					break;
				}

				lseek(fd, BLOCK_OFFSET(singleInd[k]), SEEK_SET);
				read(fd, buffer, block_size);

				for (int j = 0; j < 1024; j++)
				{
					charLido = buffer[j];
					fputc(charLido, arquivo);
					sizeTemp = sizeTemp - 1;
					if (sizeTemp <= 0)
					{
						break;
					}
				}
			}
		}
	}

	free(buffer);
}

/* Copia os dados do arquivo com nome 'nome' para arquivoDest

OBS: Precisa inserir inodeTemp em group?
*/
void copiaArquivoPorNome(struct ext2_inode *inode, struct ext2_group_desc *group, char *nome, int *grupoAtual, FILE *arquivoDest)
{
	struct ext2_group_desc *grupoTemp = (struct ext2_group_desc *)malloc(sizeof(struct ext2_group_desc));
	struct ext2_inode *inodeTemp = (struct ext2_inode *)malloc(sizeof(struct ext2_inode));

	getArquivoPorNome(inode, group, nome, grupoAtual, inodeTemp, grupoTemp);

	copiaArquivo(inodeTemp, arquivoDest);

	// DUVIDA: Não temos que colocar esta cópia do Inode no grupo?
	free(grupoTemp);
	free(inodeTemp);
}


// Função comentada: pretende exibir o conteúdo de um arquivo de nome 'nome'
void leArquivoPorNome(struct ext2_inode *inode, struct ext2_group_desc *group, char *nome, int *grupoAtual)
{
	struct ext2_group_desc *grupoTemp = (struct ext2_group_desc *)malloc(sizeof(struct ext2_group_desc));
	struct ext2_inode *inodeTemp = (struct ext2_inode *)malloc(sizeof(struct ext2_inode));

	getArquivoPorNome(inode, group, nome, grupoAtual, inodeTemp, grupoTemp);

	printaArquivo(inodeTemp);

	free(grupoTemp);
	free(inodeTemp);
	// printf("INODE: %d\n", valor);

	// printf("TAMANHO: %d\n", inode->i_size);
	// printf("TESTE: %d\n", inode->i_block[0]);
	// char *teste;
	// printf("FD: %d", fd);
	// printf("\n---%d---\n", BLOCK_OFFSET(inode->i_block[0]));
	// read(fd2, teste, block_size);
	/*for (int i = 0; i < inode->i_size; i++)
	{
		printf("%c", teste[i]);
	}*/
}

void funct_cat(struct ext2_inode *inode, struct ext2_group_desc *group, char *nome, int *grupoAtual)
{
	if(S_ISDIR(inode->i_mode)) leArquivoPorNome(inode, group, nome, grupoAtual);
	else printf("Erro: é um diretório");

}

// Funcoes de leitura

// 1 - info: exibe informacoes do disco e do sistema de arquivos
void funct_info()
{
	printf(//"Reading super-block from device " FD_DEVICE ":\n"
		   "Volume name.....: %s\n"
		   "Image size......: %u bytes\n"
		   "Free space......: %u KiB\n"
		   "Free inodes.....: %u\n"
		   "Free blocks.....: %u\n"
		   "Block size......: %u bytes\n"
		   "Inode size......: %u bytes\n"
		   "Groups count....: %u\n"
		   "Groups size.....: %u blocks\n"
		   "Groups inodes...: %u inodes\n"
		   "Inodetable size.: %lu blocks\n",

		   super.s_volume_name,
		   (super.s_blocks_count * block_size),
		   (super.s_free_blocks_count * block_size) / 1024,
		   // #BUG_CONHECIDO: é mostrado mais Free space do que o Campiolo mostra
		   super.s_free_inodes_count,
		   super.s_free_blocks_count,
		   block_size,
		   super.s_inode_size,
		   (super.s_blocks_count / super.s_blocks_per_group), // quantos / (quantos por grupo)
		   /* OBS acima: essa divisão pode retornar um a menos caso o ultimo grupo não tenha
		   exatamente todo o número de blocos certo, por causa de uma imagem não divisivel pelo tamanho.
		   #BUG_CONHECIDO: quando documentar bugs conhecidos, colocar esse.
		   */
		   super.s_blocks_per_group,
		   super.s_inodes_per_group,
		   (super.s_inodes_per_group / (block_size / sizeof(struct ext2_inode))));

	/*
	infos nao uteis pro comando
	super.s_inodes_count,
	super.s_blocks_count,
	super.s_r_blocks_count, //  reserved blocks count
	super.s_first_data_block,
	block_size,
	super.s_creator_os,
	super.s_first_ino, // first non-reserved inode
	super.s_magic);
	*/
}

// 3 - attr <file | dir>: exibe os atributos de um arquivo (file) ou diretorio (dir)
void funct_attr(struct ext2_inode *inode, struct ext2_group_desc *group, char *nome, int grupoAtual)
{
	struct ext2_group_desc *grupoTemp = (struct ext2_group_desc *)malloc(sizeof(struct ext2_group_desc));
	struct ext2_inode *inodeTemp = (struct ext2_inode *)malloc(sizeof(struct ext2_inode));
	getArquivoPorNome(inode, group, nome, &grupoAtual, inodeTemp, grupoTemp);
	char is_file = '-';
	char user_read, user_write, user_exec;
	char group_read, group_write, group_exec;
	char other_read, other_write, other_exec;
	if(S_ISDIR(inodeTemp->i_mode)) is_file = 'd';
	else is_file = 'f';
	if((inodeTemp->i_mode) & (EXT2_S_IRUSR)) user_read = 'r';
	else user_read = '-';
	if((inodeTemp->i_mode) & (EXT2_S_IWUSR)) user_write = 'w';
	else user_write = '-';
	if((inodeTemp->i_mode) & (EXT2_S_IXUSR)) user_exec = 'x';
	else user_exec = '-';
	if((inodeTemp->i_mode) & (EXT2_S_IRGRP)) group_read = 'r';
	else group_read = '-';
	if((inodeTemp->i_mode) & (EXT2_S_IWGRP)) group_write = 'w';
	else group_write = '-';
	if((inodeTemp->i_mode) & (EXT2_S_IXGRP)) group_exec = 'x';
	else group_exec = '-';
	if((inodeTemp->i_mode) & (EXT2_S_IROTH)) other_read = 'r';
	else other_read = '-';
	if((inodeTemp->i_mode) & (EXT2_S_IWOTH)) other_write = 'w';
	else other_write = '-';
	if((inodeTemp->i_mode) & (EXT2_S_IXOTH)) other_exec = 'x';
	else other_exec = '-';
	
	printf("permissões   uid   gid   tamanho   modificado em\n");
	printf("%c%c%c%c%c%c%c%c%c%c",
	is_file,
	user_read, user_write, user_exec,
	group_read, group_write, group_exec,
	other_read, other_write, other_exec
	);
	printf("   %d  ", inodeTemp->i_uid);
	printf("    %d  ", inodeTemp->i_gid);
	if(inodeTemp->i_size > 1024)
	{ 
		printf("  %.1f KiB", (((float)inodeTemp->i_size) / 1024));
	}
	else printf("  %d B ", (inodeTemp->i_size));

	// TODO: converter segundos epoch to datetime
	time_t tempo = (inodeTemp->i_mtime);
	struct tm * ptm = gmtime(&tempo);
	printf("  %d/%d/%d %d:%d", 
	ptm->tm_mday, ptm->tm_mon, (ptm->tm_year + 1900),
	ptm->tm_hour, ptm->tm_min);
	printf("\n");
	free(inodeTemp);
	free(grupoTemp);
}

// 4 - cd <path>: altera o diretorio corrente para o definido como path
void funct_cd(struct ext2_inode *inode, struct ext2_group_desc *group, int *grupoAtual, char *nome)
{
	unsigned int inodeTmp = 0;

	read_dir(inode, group, &inodeTmp, nome);

	trocaGrupo(&inodeTmp, group, grupoAtual);

	unsigned int index = ((int)inodeTmp) % super.s_inodes_per_group;

	read_inode(index, group, inode);
}

// 5 - ls: lista os arquivos e diretorios do diretorio corrente
void funct_ls(struct ext2_inode *inode, struct ext2_group_desc *group)
{
	void *block;
	if (S_ISDIR(inode->i_mode))
	{
		struct ext2_dir_entry_2 *entry;
		unsigned int size = 0;

		if ((block = malloc(block_size)) == NULL)
		{ /* allocate memory for the data block */
			fprintf(stderr, "Memory error\n");
			close(fd);
			exit(1);
		}

		lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
		read(fd, block, block_size); /* read block from disk*/

		entry = (struct ext2_dir_entry_2 *)block; /* first entry in the directory */
												  /* Notice that the list may be terminated with a NULL
													 entry (entry->inode == NULL)*/
		while ((size < inode->i_size) && entry->inode)
		{
			char file_name[EXT2_NAME_LEN + 1];
			memcpy(file_name, entry->name, entry->name_len);
			file_name[entry->name_len] = 0; /* append null character to the file name */

			// printf("%10u %s\n", entry->inode, file_name);
			printf("%s\n", file_name);
			printf("inode: %u\n", entry->inode);
			printf("record length: %u\n", entry->rec_len);
			printf("record length: %u\n", entry->name_len);
			printf("record length: %u\n", entry->file_type);
			printf("\n");
			// printf("%s", nometmp);
			entry = (ext2_dir_entry_2 *)((char *)entry + entry->rec_len);
			size += entry->rec_len;
		}

		free(block);
	}
}

/* Lida com as entradas da linha de comando

comandoPrincipal: identificador do comando
comandoInteiro: sintaxe inteira do comando
*/
int executarComando(char *comandoPrincipal, char **comandoInteiro, struct ext2_inode *inode, struct ext2_group_desc *group)
{
	if (!strcmp(comandoPrincipal, "info"))
	{
		// Exibe informações do disco e do sistema de arquivos
		funct_info();
	}
	else if (!strcmp(comandoPrincipal, "cat"))
	{
		funct_cat(inode, group, comandoInteiro[1], &grupoAtual);
	}
	else if (!strcmp(comandoPrincipal, "attr"))
	{
		funct_attr(inode, group, comandoInteiro[1], grupoAtual);
	}
	else if (!strcmp(comandoPrincipal, "cd"))
	{
		funct_cd(inode, group, &grupoAtual, comandoInteiro[1]);
	}
	else if (!strcmp(comandoPrincipal, "ls"))
	{
		funct_ls(inode, group);
	}
	else if (!strcmp(comandoPrincipal, "pwd"))
	{
		/* code */
	}
	else
	{
		printf("\nErro\n");
		return -1;
	}

	return 0;
}

int main(void)
{
	struct ext2_group_desc group;
	struct ext2_inode inode;

	char *entrada = (char *)malloc(100 * sizeof(char));		  // Comando enviado pelo terminal
	char **argumentos = (char **)malloc(10 * sizeof(char *)); // Lista de strings/argumentos do comando
	char *token;											  // Cada parte do comando;
	int numeroArgumentos = 0;								  // Número de partes do comando

	init_super(&group, &inode);

	while (1)
	{
		numeroArgumentos = 0;

		entrada = readline("[nEXT2Shell]>>> ");
		if (!strcmp(entrada, "")) // Reinicia o processo de entrada se nenhum comando for digitado;
		{
			continue;
		}

		entrada[strcspn(entrada, "\n")] = 0; // Consome o '\n' que o readline coloca;
		add_history(entrada);				 // Acrescenta o comando no histórico;

		token = strtok(entrada, " ");
		if (!(strcasecmp(token, "exit"))) // Sai quando for digitado exit;
		{
			return 0;
		}

		argumentos[numeroArgumentos] = (char *)malloc(30 * sizeof(char));
		argumentos[numeroArgumentos] = token;

		token = strtok(NULL, " ");

		while (token != NULL)
		{
			numeroArgumentos += 1;

			argumentos[numeroArgumentos] = (char *)malloc(30 * sizeof(char));
			argumentos[numeroArgumentos] = token;

			token = strtok(NULL, " ");
		}

		if (executarComando(argumentos[0], argumentos, &inode, &group) == -1)
		{
			printf("\nErro: Comando nao suportado\n");
			exit(1);
		}

		for (int i = 0; i < numeroArgumentos; i++)
		{
			free(argumentos[i]);
		}

		printf("\n");
	}

	exit(0);
}