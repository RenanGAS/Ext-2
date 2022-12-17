/**
 * Descrição: Este programa implementa comandos sobre o sistema de arquivos EXT2.
 *
 * Autor: Christofer Daniel Rodrigues Santos, Guilherme Augusto Rodrigues Maturana, Renan Guensuke Aoki Sakashita
 * Data de criação: 22/10/2022
 * Datas de atualização: 04/11/2022, 11/11/2022, 18/11/2022, 25/11/2022, 02/12/2022, 15/12/2022, 17/12/2022
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
#include <fstream>

// Shell

#include <sys/wait.h>
#include <sys/resource.h>
#include <readline/readline.h>
#include <readline/history.h>

// Vetor

#include <cstring>
#include <iostream>
#include <vector>
using namespace std;

#define BASE_OFFSET 1024											 // Localização do superbloco
#define FD_DEVICE "./myext2image.img"								 // Imagem do sistema de arquivos
#define EXT2_SUPER_MAGIC 0xEF53										 // Número mágico do EXT2
#define BLOCK_OFFSET(block) (BASE_OFFSET + (block - 1) * block_size) // Função que calcula a posição de um bloco com base em seu número
#define block_size (1024 << super.s_log_block_size)					 // Tamanho do bloco: s_log_block_size expressa o tamanho do bloco em potências de 2
																	 // Como temos que s_log_block_size = 0, temos que o tamanho do bloco é dado por 1024 * 2^0 = 1024

#define EXT2_S_IRUSR 0x0100 // user read
#define EXT2_S_IWUSR 0x0080 // user write
#define EXT2_S_IXUSR 0x0040 // user execute
#define EXT2_S_IRGRP 0x0020 // group read
#define EXT2_S_IWGRP 0x0010 // group write
#define EXT2_S_IXGRP 0x0008 // group execute
#define EXT2_S_IROTH 0x0004 // others read
#define EXT2_S_IWOTH 0x0002 // others write
#define EXT2_S_IXOTH 0x0001 // others execute

// Variáveis globais

static struct ext2_super_block super;		 // Superbloco
static int fd;								 // Descritor da imagem do sistema de arquivos
vector<string> vetorCaminhoAtual;			 // Caminho de diretórios atual
vector<ext2_dir_entry_2 *> vetorEntradasDir; // Vetor auxiliar para renomeação de arquivos
int grupoAtual = 0;							 // Variável auxiliar para armazenar o valor do Grupo de blocos atual

void read_inode_bitmap(int fd, struct ext2_group_desc *group);

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
void trocaGrupo(long int *valor, struct ext2_group_desc *group, int *grupoAtual)
{
	long int block_group = ((*valor) - 1) / super.s_inodes_per_group; // Cálculo do grupo do Inode

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
void read_dir(struct ext2_inode *inode, struct ext2_group_desc *group, long int *valorInode, char *nome)
{
	void *block;
	(*valorInode) = -1;
	if (!strlen(nome))
		*valorInode = -2;
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

// Retorna a última posição da lista de arquivos de um diretório, para inserção de um novo arquivo
int getLastEntry(struct ext2_inode *inode, struct ext2_group_desc *group)
{
	int acc = 0;
	void *block;

	// Verifica se o Inode pertence a um diretório
	if (S_ISDIR(inode->i_mode))
	{
		struct ext2_dir_entry_2 *entry;
		unsigned int size = 0;

		if ((block = malloc(block_size)) == NULL)
		{
			fprintf(stderr, "Memory error\n");
			close(fd);
			exit(1);
		}

		lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
		read(fd, block, block_size);

		entry = (struct ext2_dir_entry_2 *)block;

		while ((size < inode->i_size) && entry->inode)
		{
			if (acc + entry->rec_len < 1024)
			{
				acc += entry->rec_len;
			}

			char file_name[EXT2_NAME_LEN + 1];
			memcpy(file_name, entry->name, entry->name_len);
			file_name[entry->name_len] = 0;

			// printf("%10u %s", entry->inode, file_name);
			// printf("%s", nometmp);

			entry = (ext2_dir_entry_2 *)((char *)entry + entry->rec_len);
			size += entry->rec_len;

			// printf(" - EntryVal:%d\n", size);
			// printf(" - Acc:%d\n", acc);
		}
		// printf("EntryVal:%d", size);
		free(block);
	}
	return acc;
}

/* Escreve o inode de número inode_no na imagem

inode_no: número do Inode a ser escrito
*/
void write_inode(unsigned int inode_no, struct ext2_group_desc *group, struct ext2_inode *inode)
{
	// printf("Dentro dowriteinode %u\n", inode_no);

	lseek(fd, BLOCK_OFFSET(group->bg_inode_table) + (inode_no - 1) * sizeof(struct ext2_inode),
		  SEEK_SET);
	write(fd, inode, sizeof(struct ext2_inode));
}

/* Faz o tratamento do parâmetro passado em 'cd', modificando 'vetorCaminhoAtual' e atualizando 'valorInode'
com o valor de Inode do diretório parametrizado

valorInode: variável inicialmente zerada para receber o Inode do diretório de nome 'nome'
*/
void constroiCaminho(struct ext2_inode *inode, struct ext2_group_desc *group, long int *valorInode, const char *nome)
{
	void *block;
	*valorInode = -1;
	struct ext2_dir_entry_2 *entry;
	unsigned int size = 0;

	if ((block = malloc(block_size)) == NULL)
	{
		fprintf(stderr, "Memory error\n");
		close(fd);
		exit(1);
	}

	lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
	read(fd, block, block_size);

	entry = (struct ext2_dir_entry_2 *)block;

	int found = 0; // Verifica se achou um diretório correspondente

	while ((size < inode->i_size) && entry->inode)
	{
		char file_name[EXT2_NAME_LEN + 1];
		memcpy(file_name, entry->name, entry->name_len);
		file_name[entry->name_len] = 0;

		if (!strcmp(nome, file_name))
		{
			found = 1;

			if (entry->file_type != 2)
			{
				printf("Erro: Não é um diretório.\n");
				constroiCaminho(inode, group, valorInode, &("."[0]));
				return;
			}

			string nomeEntrada = entry->name;

			if (!strcmp(entry->name, ".."))
			{
				if (vetorCaminhoAtual.empty()) // Se estivermos no diretório 'root', não se faz nada
				{
					break;
				}

				vetorCaminhoAtual.pop_back(); // Se não for o 'root', removemos o último elemento
				break;
			}

			if (!strcmp(entry->name, "."))
			{
				break;
			}

			vetorCaminhoAtual.push_back(nomeEntrada);

			break;
		}

		entry = (ext2_dir_entry_2 *)((char *)entry + entry->rec_len);
		size += entry->rec_len;
	}
	if (!found)
		printf("Diretório não encontrado.\n");
	*valorInode = entry->inode;

	free(block);
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
	if ((fd = open(FD_DEVICE, O_RDWR)) < 0)
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
int getArquivoPorNome(struct ext2_inode *inode, struct ext2_group_desc *group, char *nome, int *grupoAtual, struct ext2_inode *novoInode, struct ext2_group_desc *novoGroup)
{
	long int valorInodeTmp;

	// Cópia das variáveis para mudança de valores de Grupo e Inode
	memcpy(novoGroup, group, sizeof(struct ext2_group_desc));
	memcpy(novoInode, inode, sizeof(struct ext2_inode));

	// grupoTemp = group;

	// Atualização do Inode para o que contém o arquivo 'nome'
	read_dir(novoInode, novoGroup, &valorInodeTmp, nome);

	// ERRO: valorInodeTmp é unsigned
	if (valorInodeTmp == -1)
	{
		return -1;
	}
	else if (valorInodeTmp == -2)
	{
		return -2;
	}

	// printf("TROCOU DE GRUPO");

	// Atualização do Groupo para o que contém o novo Inode
	trocaGrupo(&valorInodeTmp, novoGroup, grupoAtual);

	// Cálculo do index real do Inode no novo Grupo
	unsigned int index = valorInodeTmp % super.s_inodes_per_group;

	// Atualização do Inode no novo Grupo
	read_inode(index, novoGroup, novoInode);

	return 0;
}

// Copia o conteúdo dos blocos de dados em inode para arquivo
void copiaArquivo(struct ext2_inode *inode, char *arquivo)
{
	ofstream destineFile(arquivo);

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
			destineFile << charLido;

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
				destineFile << charLido;
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
					destineFile << charLido;
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

// Exibe o bitmap de blocos do grupo 'group'
void read_block_bitmap(struct ext2_group_desc *group)
{
	unsigned char *bitmap;

	// bitmap tem tamanho de um bloco
	bitmap = (unsigned char *)malloc(block_size);

	// Lê o bitmap de blocos em 'bitmap'
	lseek(fd, BLOCK_OFFSET(group->bg_block_bitmap), SEEK_SET);
	read(fd, bitmap, block_size);

	// printf("\n\n-----Lendo block bitmap-----\n\n");

	// Exemplo:
	// a = 10110011
	// j = 0
	// a >> j = 10110011
	// 10110011 & 00000001 = 00000001
	// !!(00000001) = 00000001

	// Percorre todos os bytes do bitmap
	for (int i = 0; i < 1024; i++)
	{
		char a = bitmap[i];
		printf("%d - ", i);

		// Exibe os bits que indicam o estado de cada Bloco
		for (int j = 0; j < 8; j++)
		{
			printf("%d ", !!((a >> j) & 0x01));
		}
		printf("\n");
	}

	// printf("\n\n-----Terminou block bitmap-----\n\n");
	free(bitmap);
}

// Exibe o bitmap de Inodes do grupo 'group'
void read_inode_bitmap(struct ext2_group_desc *group)
{
	char *bitmap;

	bitmap = (char *)malloc(block_size);

	// Lê o bitmap de inodes em 'bitmap'
	lseek(fd, BLOCK_OFFSET(group->bg_inode_bitmap), SEEK_SET);
	read(fd, bitmap, block_size);

	// printf("\n\n-----Lendo inode bitmap-----\n\n");

	// Percorre todos os bytes do bitmap
	for (int i = 0; i < 1024; i++)
	{
		char a = bitmap[i];
		printf("%d - ", i);

		// Exibe os bits que indicam o estado de cada Inode
		for (int j = 0; j < 8; j++)
		{
			printf("%d ", !!((a >> j) & 0x01));
		}
		printf("\n");
	}

	// printf("\n\n-----Terminou inode bitmap-----\n\n");
	free(bitmap);
}

// Retorna o offset do primeiro Inode livre no bitmap de Inodes
int find_free_inode(struct ext2_group_desc *group)
{
	char *bitmap;

	bitmap = (char *)malloc(block_size);

	lseek(fd, BLOCK_OFFSET(group->bg_inode_bitmap), SEEK_SET);
	read(fd, bitmap, block_size);

	int buscar = 1;

	// printf("\n\n-----Procurando inode livre no bitmap-----\n\n");

	for (int i = 0; i < 1024 && buscar; i++)
	{
		char a = bitmap[i];

		for (int j = 0; j < 8 && buscar; j++)
		{
			if (!((a >> j) & 0x01)) // Se achou um bit 0, para a busca e retorna o offset
			{						// do Inode no bitmap de Inodes
				buscar = 0;
				return ((8 * i) + (j));
			}
		}
	}

	// printf("\n\n-----Terminou a busca por Inode livre no inode bitmap-----\n\n");
	free(bitmap);

	return 0;
}

// Retorna o offset do primeiro Bloco livre no bitmap de Blocos
int find_free_block(int fd, struct ext2_group_desc *group)
{
	unsigned char *bitmap;

	int buscar = 1;

	bitmap = (unsigned char *)malloc(block_size);
	lseek(fd, BLOCK_OFFSET(group->bg_block_bitmap), SEEK_SET);
	read(fd, bitmap, block_size);

	// printf("\n\n-----Procurando blocos livres no bitmap de blocos-----\n\n");

	for (int i = 0; i < 1024 && buscar; i++)
	{
		char a = bitmap[i];

		for (int j = 0; j < 8 && buscar; j++)
		{
			if (!((a >> j) & 0x01)) // Se achou um bit 0, para a busca e retorna o offset
			{						// do Bloco no bitmap de Blocos
				buscar = 0;
				return ((8 * i) + (j));
			}
		}
	}

	// printf("\n\n-----Terminou a busca por blocos livre no bitmap de blocos-----\n\n");
	free(bitmap);

	return 0;
}

// Marca a posição bitVal no bitmap de Blocos como ocupada
void set_block_bitmap(struct ext2_group_desc *group, int bitVal)
{
	char *bitmap;

	int y = bitVal / 8; // Pega o byte em que se encontra o bloco
	int x = bitVal % 8; // Pega o offset no byte

	int marcado = (0x1 << x); // Transforma o offset em número binário

	// printf("\n--MARCADO %X", marcado);

	bitmap = (char *)malloc(block_size);

	// Lê o bitmap de blocos
	lseek(fd, BLOCK_OFFSET(group->bg_block_bitmap), SEEK_SET);
	read(fd, bitmap, block_size);

	// Pega o byte correspondente de bitVal
	char tmp = bitmap[y];
	// printf("\n\nTMP:%d", tmp);

	tmp = (tmp | marcado); // Constrói o valor do byte de 'bitVal' que teríamos se estivesse 'ocupado'
	bitmap[y] = tmp;	   // Armazena no bitmap
	// printf("\n\nTMP:%d", tmp);

	// Atualiza o bitmap
	lseek(fd, BLOCK_OFFSET(group->bg_block_bitmap), SEEK_SET);
	write(fd, bitmap, block_size);
	// tmp = (tmp << j) & 0x80

	free(bitmap);
}

// Marca a posição bitVal no bitmap de Inodes como ocupada
void set_inode_bitmap(struct ext2_group_desc *group, int bitVal)
{
	char *bitmap;
	int y = bitVal / 8; // Pega o byte em que se encontra o Inode
	int x = bitVal % 8; // Pega o offset no byte

	int marcado = (0x1 << x);

	// printf("\n--MARCADO %X", marcado);

	bitmap = (char *)malloc(block_size);

	lseek(fd, BLOCK_OFFSET(group->bg_inode_bitmap), SEEK_SET);
	read(fd, bitmap, block_size);

	char tmp = bitmap[y];
	// printf("\n\nTMP:%d", tmp);

	tmp = (tmp | marcado);
	bitmap[y] = tmp;
	// printf("\n\nTMP:%d", tmp);

	lseek(fd, BLOCK_OFFSET(group->bg_inode_bitmap), SEEK_SET);
	write(fd, bitmap, block_size);
	// tmp = (tmp << j) & 0x80

	free(bitmap);
}

// Cacula o número a ser somado ao tamanho do nome do arquivo para que a entrada tenha tamanho múltiplo de 4
int roundLen(int tamanho)
{
	return (tamanho % 4 == 0) ? 0 : 4 - (tamanho % 4);
}

// PAREI AQUI
void rewriteSuperAndGroup(struct ext2_group_desc *group, int gropNum)
{
	lseek(fd, BASE_OFFSET + block_size + sizeof(struct ext2_group_desc) * gropNum, SEEK_SET);
	write(fd, group, sizeof(struct ext2_group_desc));

	lseek(fd, BASE_OFFSET, SEEK_SET);
	write(fd, &super, sizeof(struct ext2_super_block));
}

void funct_mkdir(struct ext2_inode *inode, struct ext2_group_desc *group, char *nome, int numGrupo)
{
	struct ext2_dir_entry_2 *entryTmp = (struct ext2_dir_entry_2 *)malloc(sizeof(struct ext2_dir_entry_2));
	struct ext2_inode *inodeTemp = (struct ext2_inode *)malloc(sizeof(struct ext2_inode));
	struct ext2_inode *inodeC = (struct ext2_inode *)malloc(sizeof(struct ext2_inode));

	void *producedBlock;
	struct ext2_dir_entry_2 *producedEntry;

	char *nomeFinal;
	int tamNome;
	int arredondamento = 0;
	int tamanho = 0;
	int inodeVal = 0;
	long existe = 0;
	int blockVal = 0;

	read_dir(inode, group, &existe, nome);

	if (existe != -1)
	{
		printf("ARQUIVO JA EXISTENTE");
		return;
	}

	inodeVal = find_free_inode(group) + 1;
	printf("\n\n-----inodeVal: %d\n", inodeVal);

	set_inode_bitmap(group, (inodeVal - 1));

	tamNome = strlen(nome);
	arredondamento = roundLen(8 + tamNome);
	nomeFinal = (char *)malloc((tamNome + arredondamento) * sizeof(char));
	strcpy(nomeFinal, nome);
	for (int i = 0; i < arredondamento; i++)
	{
		strcat(nomeFinal, "\0");
	}

	producedBlock = malloc(block_size);
	producedEntry = (struct ext2_dir_entry_2 *)producedBlock;
	producedEntry = (ext2_dir_entry_2 *)((char *)producedEntry + 0);
	producedEntry->file_type = 2;
	producedEntry->name_len = 1;
	producedEntry->rec_len = 12;
	memcpy(producedEntry->name, ".\0\0\0", 4);
	producedEntry->inode = inodeVal;

	producedEntry = (ext2_dir_entry_2 *)((char *)producedEntry + producedEntry->rec_len);
	producedEntry->file_type = 2;
	producedEntry->name_len = 2;
	producedEntry->rec_len = 1012;
	memcpy(producedEntry->name, "..\0\0", 4);
	producedEntry->inode = 2;

	/*producedEntry = (void *)producedEntry + producedEntry->rec_len;
	producedEntry->file_type = 2;
	producedEntry->name_len = 2;
	producedEntry->rec_len = 1000;
	memcpy(producedEntry->name, "...\0", 4);
	producedEntry->inode = 2;*/

	blockVal = find_free_block(fd, group) + 1;
	printf("\n\n-----BlockVal: %d\n", blockVal);

	set_block_bitmap(group, (blockVal - 1));
	lseek(fd, BLOCK_OFFSET(blockVal), SEEK_SET);
	write(fd, producedBlock, block_size);

	int temp = getLastEntry(inode, group);
	// tamNome = strlen(nome);

	void *block;
	if (S_ISDIR(inode->i_mode))
	{
		struct ext2_dir_entry_2 *entry;
		// struct ext2_dir_entry_2 *entry2 = malloc(sizeof(struct ext2_dir_entry_2));
		unsigned int size = 0;

		if ((block = malloc(block_size)) == NULL)
		{ /* allocate memory for the data block */
			fprintf(stderr, "Memory error\n");
			close(fd);
			exit(1);
		}

		lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
		read(fd, block, block_size); /* read block from disk*/

		entry = (struct ext2_dir_entry_2 *)block;

		// memcpy(producedBlock, entry, block_size);
		// lseek(fd, BLOCK_OFFSET(blockVal), SEEK_SET);
		// write(fd, producedBlock, block_size);

		entry = (ext2_dir_entry_2 *)((char *)entry + temp);
		entry->rec_len = 8 + entry->name_len;
		entry->rec_len = entry->rec_len + roundLen(entry->rec_len);

		inodeTemp->i_block[0] = blockVal;
		inodeTemp->i_block[1] = 0;
		inodeTemp->i_block[2] = 0;
		inodeTemp->i_block[3] = 0;
		inodeTemp->i_block[4] = 0;
		inodeTemp->i_block[5] = 0;
		inodeTemp->i_block[6] = 0;
		inodeTemp->i_block[7] = 0;
		inodeTemp->i_block[8] = 0;
		inodeTemp->i_block[9] = 0;
		inodeTemp->i_block[10] = 0;
		inodeTemp->i_block[11] = 0;
		inodeTemp->i_block[12] = 0;
		inodeTemp->i_block[13] = 0;
		inodeTemp->i_block[14] = 0;
		inodeTemp->i_atime = 1668912196;
		inodeTemp->i_blocks = 2;
		inodeTemp->i_ctime = 1668911978;
		inodeTemp->i_dir_acl = 0;
		inodeTemp->i_dtime = 0;
		inodeTemp->i_faddr = 0;
		inodeTemp->i_file_acl = 0;
		inodeTemp->i_flags = 0;
		inodeTemp->i_generation = -1833064728;
		inodeTemp->i_gid = 0;
		inodeTemp->i_links_count = 2;
		inodeTemp->i_mode = 16877;
		inodeTemp->i_mtime = 1668911978;
		inodeTemp->i_size = 1024;
		inodeTemp->i_uid = 0;
		write_inode(inodeVal, group, inodeTemp);

		temp = temp + entry->rec_len;
		entry = (ext2_dir_entry_2 *)((char *)entry + entry->rec_len);
		entry->inode = inodeVal;
		entry->name_len = tamNome;
		entry->rec_len = 1024 - temp;
		memcpy(entry->name, nomeFinal, tamNome * sizeof(char));
		entry->file_type = 2;

		lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
		write(fd, block, block_size);
	}

	group->bg_free_blocks_count = group->bg_free_blocks_count - 1;
	super.s_free_blocks_count = super.s_free_blocks_count - 1;

	group->bg_free_inodes_count = group->bg_free_inodes_count - 1;
	super.s_free_inodes_count = super.s_free_inodes_count - 1;

	rewriteSuperAndGroup(group, numGrupo);

	free(inodeTemp);
	free(entryTmp);
}

void funct_touch(struct ext2_inode *inode, struct ext2_group_desc *group, char *nome, int grupoAtual)
{
	struct ext2_dir_entry_2 *entryTmp = (struct ext2_dir_entry_2 *)malloc(sizeof(struct ext2_dir_entry_2));
	struct ext2_inode *inodeTemp = (struct ext2_inode *)malloc(sizeof(struct ext2_inode));
	struct ext2_inode *inodeC = (struct ext2_inode *)malloc(sizeof(struct ext2_inode));
	char *nomeFinal;
	int tamNome;
	int arredondamento = 0;
	int tamanho = 0;
	int inodeVal = 0;
	long existe = 0;

	read_dir(inode, group, &existe, nome);

	if (existe != -1)
	{
		printf("ARQUIVO JA EXISTENTE");
		return;
	}

	// entryTmp->inode

	printf("\n\n-----inodeBITMAP");
	// read_inode_bitmap(fd, group);

	inodeVal = find_free_inode(group) + 1;
	printf("\n\n-----inodeVal: %d", inodeVal);

	set_inode_bitmap(group, (inodeVal - 1));
	// write_inode(fd, inodeVal, group, inodeTemp);
	//  int blocktmp = find_free_block(fd, group);
	//  set_block_bitmap(fd, group, blocktmp);

	tamNome = strlen(nome);
	arredondamento = roundLen(8 + tamNome);
	nomeFinal = (char *)malloc((tamNome + arredondamento) * sizeof(char));
	strcpy(nomeFinal, nome);
	for (int i = 0; i < arredondamento; i++)
	{
		strcat(nomeFinal, "\0");
	}

	// memcpy(nomeFinal, nome, sizeof(char) * tamNome);

	int temp = getLastEntry(inode, group);
	tamNome = strlen(nome);

	void *block;
	if (S_ISDIR(inode->i_mode))
	{
		struct ext2_dir_entry_2 *entry;
		// struct ext2_dir_entry_2 *entry2 = malloc(sizeof(struct ext2_dir_entry_2));
		unsigned int size = 0;

		if ((block = malloc(block_size)) == NULL)
		{ /* allocate memory for the data block */
			fprintf(stderr, "Memory error\n");
			close(fd);
			exit(1);
		}

		lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
		read(fd, block, block_size); /* read block from disk*/

		entry = (struct ext2_dir_entry_2 *)block;
		entry = (ext2_dir_entry_2 *)((char *)entry + temp);
		entry->rec_len = 8 + entry->name_len;
		entry->rec_len = entry->rec_len + roundLen(entry->rec_len);
		// memcpy(entry->name, "heuuo.txt", 9*sizeof(char));

		// entryTmp->inode = inodeVal;
		// entryTmp->name_len = tamNome;
		// entryTmp->rec_len = 8 + entryTmp->name_len;
		// entryTmp->rec_len = entryTmp->rec_len + roundLen(entryTmp->rec_len);
		// tamanho = entryTmp->rec_len;
		// entryTmp->file_type = 1;

		// memcpy(entryTmp->name, nomeFinal, entryTmp->name_len);

		// memcpy(entryTmp, entry, sizeof(struct ext2_dir_entry_2));
		// read_inode(fd, entry->inode, group, inodeC);
		// printInode(inodeTemp);
		// inodeTemp->i_mode = 33188;
		// inodeTemp->i_size = 0;
		// inodeTemp->i_gid = 0;
		// inodeTemp->i_uid = 0;
		inodeTemp->i_block[0] = 0;
		inodeTemp->i_block[1] = 0;
		inodeTemp->i_block[2] = 0;
		inodeTemp->i_block[3] = 0;
		inodeTemp->i_block[4] = 0;
		inodeTemp->i_block[5] = 0;
		inodeTemp->i_block[6] = 0;
		inodeTemp->i_block[7] = 0;
		inodeTemp->i_block[8] = 0;
		inodeTemp->i_block[9] = 0;
		inodeTemp->i_block[10] = 0;
		inodeTemp->i_block[11] = 0;
		inodeTemp->i_block[12] = 0;
		inodeTemp->i_block[13] = 0;
		inodeTemp->i_block[14] = 0;
		inodeTemp->i_atime = 1668911917;
		inodeTemp->i_blocks = 0;
		inodeTemp->i_ctime = 1668911917;
		inodeTemp->i_dir_acl = 0;
		inodeTemp->i_dtime = 0;
		inodeTemp->i_faddr = 0;
		inodeTemp->i_file_acl = 0;
		inodeTemp->i_flags = 0;
		inodeTemp->i_generation = -1280917867;
		inodeTemp->i_gid = 0;
		inodeTemp->i_links_count = 1;
		inodeTemp->i_mode = 33188;
		inodeTemp->i_mtime = 1668911917;
		inodeTemp->i_size = 0;
		inodeTemp->i_uid = 0;
		write_inode(inodeVal, group, inodeTemp);

		temp = temp + entry->rec_len;
		entry = (ext2_dir_entry_2 *)((char *)entry + entry->rec_len);
		entry->inode = inodeVal;
		entry->name_len = tamNome;
		entry->rec_len = 1024 - temp;
		memcpy(entry->name, nomeFinal, tamNome * sizeof(char));
		entry->file_type = 1;

		lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
		write(fd, block, block_size);
	}

	group->bg_free_inodes_count = group->bg_free_inodes_count - 1;
	super.s_free_inodes_count = super.s_free_inodes_count - 1;

	rewriteSuperAndGroup(group, grupoAtual);

	free(inodeTemp);
	free(entryTmp);
}

int isLoaded(struct ext2_inode *inode, struct ext2_group_desc *group)
{
	printf("\n\n---ls---\n");
	int contador = 0;
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
			// printf("%s", nometmp);
			entry = (ext2_dir_entry_2 *)((char *)entry + entry->rec_len);
			size += entry->rec_len;
			contador++;
		}
		// read_inode_bitmap(fd, group);
		free(block);
		printf("Contador-2: %d\n", (contador - 2));
		return (contador - 2);
	}
	return -1;
}

void trocaGrupoBlock(int valor, struct ext2_group_desc *group, int *grupoAtual)
{
	// printf("\n--- TROCANDO O GRUPO ---\n");
	// printf("INODE: %u\n", valor);
	unsigned int block_group = (valor - 1) / super.s_blocks_per_group;
	if (block_group != (*grupoAtual))
	{
		// printf("trocou de grupo\n");
		*grupoAtual = block_group;
		printf("%d\n", block_group);
		lseek(fd, BASE_OFFSET + block_size + sizeof(struct ext2_group_desc) * block_group, SEEK_SET);
		read(fd, group, sizeof(struct ext2_group_desc));
	}
}

void unset_block_bitmap(struct ext2_group_desc *group, int bitVal)
{
	printf("\n---Bloco deletado: %d---\n", bitVal);
	char *bitmap;
	int y = (bitVal) / 8;
	int x = (bitVal) % 8;
	printf("\n x:%d, y:%d", x, y);
	int marcado = (0xFE << x);
	marcado = marcado | (0xFF >> (8 - x));
	printf("\n--MARCADO %X", marcado);

	bitmap = (char *)malloc(block_size); /* allocate memory for the bitmap */
	lseek(fd, BLOCK_OFFSET(group->bg_block_bitmap), SEEK_SET);
	read(fd, bitmap, block_size);

	char tmp = bitmap[y];
	printf("\n\nTMP:%d", tmp);
	tmp = (tmp & marcado);
	bitmap[y] = tmp;
	printf("\n\nTMP:%d", tmp);

	lseek(fd, BLOCK_OFFSET(group->bg_block_bitmap), SEEK_SET);
	write(fd, bitmap, block_size);
	// tmp = (tmp << j) & 0x80

	free(bitmap);
}

void removeEntry(struct ext2_inode *inode, struct ext2_group_desc *group, char *nome)
{
	printf("\n\n---RemoveENtry---\n");
	void *block;
	void *newBlock;
	int acc = 0;
	int acc2 = 0;
	int lastEntry = 0;
	int removedSize = 0;
	int lastEntrySize = 0;
	int ultimoTam = 0;
	int removido = 0;

	lastEntry = getLastEntry(inode, group);

	if (S_ISDIR(inode->i_mode))
	{
		struct ext2_dir_entry_2 *entry;
		struct ext2_dir_entry_2 *newEntry;
		unsigned int size = 0;

		if ((block = malloc(block_size)) == NULL)
		{ /* allocate memory for the data block */
			fprintf(stderr, "Memory error\n");
			close(fd);
			exit(1);
		}

		if ((newBlock = malloc(block_size)) == NULL)
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

		// lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
		// read(fd, newBlock, block_size);
		newEntry = (struct ext2_dir_entry_2 *)newBlock;
		while ((size < inode->i_size) && entry->inode)
		{
			if (acc != acc2)
			{
				removido = 1;
			}
			// ultimoTam = entry->rec_len;
			char file_name[EXT2_NAME_LEN + 1];
			memcpy(file_name, entry->name, entry->name_len);
			file_name[entry->name_len] = 0; /* append null character to the file name */

			if (strcmp(file_name, nome) != 0)
			{
				newEntry->file_type = entry->file_type;
				newEntry->inode = entry->inode;
				newEntry->name_len = entry->name_len;
				newEntry->rec_len = entry->rec_len;
				memcpy(newEntry->name, entry->name, entry->name_len + roundLen(8 + entry->name_len));
				printf("%s\n", file_name);
				printf("inode: %u\n", entry->inode);
				printf("record length: %u\n", entry->rec_len);
				printf("name length: %u\n", entry->name_len);
				printf("file type: %u\n", entry->file_type);
				printf("\n");

				ultimoTam = newEntry->rec_len;
				acc2 += newEntry->rec_len;
				newEntry = (ext2_dir_entry_2 *)((char *)newEntry + newEntry->rec_len);
				printf("DENTRODOIF\n");
			}
			else
			{
				removedSize += entry->rec_len;
			}

			acc += entry->rec_len;
			entry = (ext2_dir_entry_2 *)((char *)entry + entry->rec_len);
			size += entry->rec_len;
			if (acc == lastEntry)
			{
				printf("IGUAL\n");
				lastEntrySize = entry->rec_len;
			}

			printf("ACC: %d\nACC2:%d\n", acc, acc2);
		}

		printf("ACC2: %d\n", acc2);

		lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
		write(fd, newBlock, block_size); /* read block from disk*/

		lastEntry = getLastEntry(inode, group);
		printf("UltimoTam: %d\n", ultimoTam);
		printf("Removido %d", removido);

		lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
		read(fd, newBlock, block_size);
		newEntry = (struct ext2_dir_entry_2 *)newBlock;

		if (lastEntrySize == removedSize)
		{
			printf("TAMANHO IGUAL\n");
			newEntry = (ext2_dir_entry_2 *)((char *)newEntry + (acc2 - ultimoTam));
			newEntry->rec_len = 1024 - acc2 + ultimoTam;
		}
		else
		{
			newEntry = (ext2_dir_entry_2 *)((char *)newEntry + (acc2 - ultimoTam));
			newEntry->rec_len = newEntry->rec_len + removedSize;
		}

		lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
		write(fd, newBlock, block_size);
		// read_inode_bitmap(fd, group);
		free(block);
	}
}

void unset_inode_bitmap(struct ext2_group_desc *group, int bitVal)
{
	printf("\n---Inode deletado: %d---\n", bitVal);
	char *bitmap;
	int y = (bitVal) / 8;
	int x = (bitVal) % 8;
	printf("\n x:%d, y:%d", x, y);
	int marcado = (0xFE << x);
	marcado = marcado | (0xFF >> (8 - x));
	printf("\n--MARCADO %X", marcado);

	bitmap = (char *)malloc(block_size); /* allocate memory for the bitmap */
	lseek(fd, BLOCK_OFFSET(group->bg_inode_bitmap), SEEK_SET);
	read(fd, bitmap, block_size);

	char tmp = bitmap[y];
	printf("\n\nTMP:%d", tmp);
	tmp = (tmp & marcado);
	bitmap[y] = tmp;
	printf("\n\nTMP:%d", tmp);

	lseek(fd, BLOCK_OFFSET(group->bg_inode_bitmap), SEEK_SET);
	write(fd, bitmap, block_size);
	// tmp = (tmp << j) & 0x80

	group->bg_free_inodes_count = group->bg_free_inodes_count + 1;
	super.s_free_inodes_count = super.s_free_inodes_count + 1;
	free(bitmap);
}

void funct_rmdir(struct ext2_inode *inode, struct ext2_group_desc *group, char *nome, int grupoAtual)
{
	printf("\n\n---rm---\n");
	int achado = 0;
	int numGrupo = grupoAtual;
	int terminou = 0;
	int numblocos = 0;
	long valorInodeTmp;
	struct ext2_group_desc *grupoTemp = (struct ext2_group_desc *)malloc(sizeof(struct ext2_group_desc));
	struct ext2_inode *inodeTemp = (struct ext2_inode *)malloc(sizeof(struct ext2_inode));
	memcpy(grupoTemp, group, sizeof(struct ext2_group_desc));
	memcpy(inodeTemp, inode, sizeof(struct ext2_inode));
	// grupoTemp = group;
	read_dir(inodeTemp, grupoTemp, &valorInodeTmp, nome);
	trocaGrupo(&valorInodeTmp, grupoTemp, &numGrupo);
	unsigned int index = valorInodeTmp % super.s_inodes_per_group;
	read_inode(index, grupoTemp, inodeTemp);
	numblocos = inodeTemp->i_blocks;
	printInode(inodeTemp);
	printf("VALOR INODE TEMP %d", valorInodeTmp);
	if (valorInodeTmp == -1)
	{
		printf("ARQUIVO NÃO ENCONTRADO");
		return;
	}

	if (!isLoaded(inodeTemp, grupoTemp))
	{

		// printf("TROCOU DE GRUPO");

		printf("\nBLOCK NUM %d", inodeTemp->i_block[0]);
		trocaGrupoBlock(inodeTemp->i_block[0], grupoTemp, &numGrupo);
		unset_block_bitmap(grupoTemp, inodeTemp->i_block[0]);

		printf("IsEmprt2: %d", isLoaded(inodeTemp, grupoTemp));

		removeEntry(inode, group, nome);
		unset_inode_bitmap(group, valorInodeTmp);
		rewriteSuperAndGroup(grupoTemp, grupoAtual);
	}
	else
	{
		printf("DIRETORIO NÃO ESTÁ VAZIO");
	}

	//  free(block);
	free(inodeTemp);
	free(grupoTemp);
}

void funct_rm(struct ext2_inode *inode, struct ext2_group_desc *group, char *nome, int grupoAtual)
{
	printf("\n\n---rm---\n");
	int achado = 0;
	int numGrupo = grupoAtual;
	int terminou = 0;
	int numblocos = 0;
	int fileSize = 0;
	long valorInodeTmp;
	struct ext2_group_desc *grupoTemp = (struct ext2_group_desc *)malloc(sizeof(struct ext2_group_desc));
	struct ext2_inode *inodeTemp = (struct ext2_inode *)malloc(sizeof(struct ext2_inode));
	memcpy(grupoTemp, group, sizeof(struct ext2_group_desc));
	memcpy(inodeTemp, inode, sizeof(struct ext2_inode));
	// grupoTemp = group;
	read_dir(inodeTemp, grupoTemp, &valorInodeTmp, nome);
	if (valorInodeTmp == -1)
	{
		printf("ARQUIVO NÃO ENCONTRADO");
		return;
	}

	// printf("TROCOU DE GRUPO");
	trocaGrupo(&valorInodeTmp, grupoTemp, &numGrupo);
	unsigned int index = valorInodeTmp % super.s_inodes_per_group;
	read_inode(index, grupoTemp, inodeTemp);
	numblocos = inodeTemp->i_blocks;
	printInode(inodeTemp);
	unsigned int *singleInd = (unsigned int *)malloc(sizeof(unsigned int) * 256);
	unsigned int *doubleInd = (unsigned int *)malloc(sizeof(unsigned int) * 256);
	printf("VALOR INODE TEMP %d", valorInodeTmp);
	fileSize = inodeTemp->i_size;

	for (int i = 0; i < 12 && fileSize > 0; i++)
	{
		printf("\nBLOCK NUM %d", inodeTemp->i_block[i]);
		trocaGrupoBlock(inodeTemp->i_block[i], grupoTemp, &numGrupo);
		unset_block_bitmap(grupoTemp, inodeTemp->i_block[i]);
		rewriteSuperAndGroup(grupoTemp, numGrupo);
		fileSize -= block_size;
	}

	lseek(fd, BLOCK_OFFSET(inodeTemp->i_block[12]), SEEK_SET);
	read(fd, singleInd, block_size);
	for (int i = 0; i < 256 && fileSize > 0; i++)
	{
		trocaGrupoBlock(singleInd[i], grupoTemp, &numGrupo);
		unset_block_bitmap(grupoTemp, singleInd[i]);
		rewriteSuperAndGroup(grupoTemp, numGrupo);
		fileSize -= block_size;
	}

	lseek(fd, BLOCK_OFFSET(inodeTemp->i_block[13]), SEEK_SET);
	read(fd, doubleInd, block_size);
	for (int i = 0; i < 256 && fileSize > 0; i++)
	{
		lseek(fd, BLOCK_OFFSET(doubleInd[i]), SEEK_SET);
		read(fd, singleInd, block_size);
		for (int k = 0; k < 256 && fileSize > 0; k++)
		{
			trocaGrupoBlock(singleInd[k], grupoTemp, &numGrupo);
			unset_block_bitmap(grupoTemp, singleInd[k]);
			rewriteSuperAndGroup(grupoTemp, numGrupo);
			fileSize -= block_size;
		}
	}

	removeEntry(inode, group, nome);
	trocaGrupo(&valorInodeTmp, grupoTemp, &numGrupo);
	unset_inode_bitmap(grupoTemp, valorInodeTmp);
	rewriteSuperAndGroup(grupoTemp, grupoAtual);
	// free(block);
	free(inodeTemp);
	free(grupoTemp);
}

/* Copia os dados do arquivo com nome 'nome' para arquivoDest

OBS: Precisa inserir inodeTemp em group?
*/
void funct_cp(struct ext2_inode *inode, struct ext2_group_desc *group, char *nome, int *grupoAtual, char *arquivoDest)
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
	struct ext2_group_desc *grupoTemp = (struct ext2_group_desc *)malloc(sizeof(struct ext2_group_desc));
	struct ext2_inode *inodeTemp = (struct ext2_inode *)malloc(sizeof(struct ext2_inode));

	int retorno = getArquivoPorNome(inode, group, nome, grupoAtual, inodeTemp, grupoTemp);
	if (retorno == -1)
	{
		printf("Arquivo não encontrado\n");
		return;
	}
	if (retorno == -2)
	{
		printf("Sintax incorreta\n");
		return;
	}

	if (S_ISDIR(inodeTemp->i_mode))
	{
		printf("Erro: É um diretório\n");
		return;
	}

	printaArquivo(inodeTemp);

	free(grupoTemp);
	free(inodeTemp);
}

// Funcoes de leitura

// 1 - info: exibe informacoes do disco e do sistema de arquivos
void funct_info()
{
	printf( //"Reading super-block from device " FD_DEVICE ":\n"
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
		((super.s_free_blocks_count - super.s_r_blocks_count) * block_size) / 1024,
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
	if (S_ISDIR(inodeTemp->i_mode))
		is_file = 'd';
	else
		is_file = 'f';
	if ((inodeTemp->i_mode) & (EXT2_S_IRUSR))
		user_read = 'r';
	else
		user_read = '-';
	if ((inodeTemp->i_mode) & (EXT2_S_IWUSR))
		user_write = 'w';
	else
		user_write = '-';
	if ((inodeTemp->i_mode) & (EXT2_S_IXUSR))
		user_exec = 'x';
	else
		user_exec = '-';
	if ((inodeTemp->i_mode) & (EXT2_S_IRGRP))
		group_read = 'r';
	else
		group_read = '-';
	if ((inodeTemp->i_mode) & (EXT2_S_IWGRP))
		group_write = 'w';
	else
		group_write = '-';
	if ((inodeTemp->i_mode) & (EXT2_S_IXGRP))
		group_exec = 'x';
	else
		group_exec = '-';
	if ((inodeTemp->i_mode) & (EXT2_S_IROTH))
		other_read = 'r';
	else
		other_read = '-';
	if ((inodeTemp->i_mode) & (EXT2_S_IWOTH))
		other_write = 'w';
	else
		other_write = '-';
	if ((inodeTemp->i_mode) & (EXT2_S_IXOTH))
		other_exec = 'x';
	else
		other_exec = '-';

	printf("permissões   uid   gid    tamanho    modificado em\n");
	printf("%c%c%c%c%c%c%c%c%c%c",
		   is_file,
		   user_read, user_write, user_exec,
		   group_read, group_write, group_exec,
		   other_read, other_write, other_exec);
	printf("    %d  ", inodeTemp->i_uid);
	printf("    %d ", inodeTemp->i_gid);
	if (inodeTemp->i_size > 1024)
	{
		printf("   %.1f KiB", (((float)inodeTemp->i_size) / 1024));
	}
	else
		printf("    %d B ", (inodeTemp->i_size));

	// TODO: converter segundos epoch to datetime
	time_t tempo = (inodeTemp->i_mtime);

	struct tm *ptm = localtime(&tempo);
	printf("  %d/%d/%d %d:%d",
		   ptm->tm_mday, ptm->tm_mon + 1, (ptm->tm_year + 1900),
		   ptm->tm_hour, ptm->tm_min);
	printf("\n");
	free(inodeTemp);
	free(grupoTemp);
}

// 4 - cd <path>: altera o diretorio corrente para o definido como path
void funct_cd(struct ext2_inode *inode, struct ext2_group_desc *group, int *grupoAtual, char *nome)
{
	long int inodeTmp = 0;

	constroiCaminho(inode, group, &inodeTmp, nome);

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
			printf("name length: %u\n", entry->name_len);
			printf("file type: %u\n", entry->file_type);
			printf("\n");
			// printf("%s", nometmp);
			entry = (ext2_dir_entry_2 *)((char *)entry + entry->rec_len);
			size += entry->rec_len;
		}

		free(block);
	}
}

void constroiListaDiretorios(struct ext2_inode *inode, struct ext2_group_desc *group, char *nomeArquivo, int *posArquivo)
{
	void *block;

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
	int contador = 0;
	while ((size < inode->i_size) && entry->inode)
	{
		struct ext2_dir_entry_2 *proxEntry = (struct ext2_dir_entry_2 *)malloc(sizeof(ext2_dir_entry_2));
		proxEntry = entry;

		char file_name[EXT2_NAME_LEN + 1];
		memcpy(file_name, entry->name, entry->name_len);
		file_name[entry->name_len] = 0;

		if (!strcmp(nomeArquivo, file_name))
		{
			*posArquivo = contador;
		}

		vetorEntradasDir.push_back(proxEntry);

		entry = (ext2_dir_entry_2 *)((char *)entry + entry->rec_len);
		size += entry->rec_len;
		contador++;
	}

	free(block);

	return;
}

unsigned int converteParaMult4(unsigned int tamanho)
{
	int qtdDe4 = tamanho / 4;
	int emTermosde4 = qtdDe4 * 4;

	return (tamanho - (emTermosde4)) > 0 ? (emTermosde4 + 4) : emTermosde4;
}

void removeAtualizaListaDiretorios(struct ext2_inode *inode, struct ext2_group_desc *group, char *novoNomeArquivo, char *nomeArquivo, int pos)
{
	struct ext2_dir_entry_2 *entry_dir_modificado = (struct ext2_dir_entry_2 *)malloc(sizeof(struct ext2_dir_entry_2));

	entry_dir_modificado->inode = vetorEntradasDir[pos]->inode;
	entry_dir_modificado->file_type = vetorEntradasDir[pos]->file_type;
	strcpy(entry_dir_modificado->name, novoNomeArquivo);

	int lenNovoNomeArquivo = strlen(novoNomeArquivo);

	entry_dir_modificado->name_len = lenNovoNomeArquivo;

	unsigned short recLenDirs = 12;

	for (auto i = vetorEntradasDir.begin(); i != vetorEntradasDir.end(); ++i)
	{
		if (!strcmp((*i)->name, vetorEntradasDir[pos]->name))
		{
			vetorEntradasDir.erase(i);
			break;
		}
	}

	vetorEntradasDir[vetorEntradasDir.size() - 1]->rec_len = 8 + converteParaMult4(vetorEntradasDir[vetorEntradasDir.size() - 1]->name_len);

	for (int i = 0; i < vetorEntradasDir.size(); i++)
	{
		recLenDirs += vetorEntradasDir[i]->rec_len;
	}

	entry_dir_modificado->rec_len = 1024 - recLenDirs;

	vetorEntradasDir.push_back(entry_dir_modificado);

	return;
}

void atualizaListaReal(struct ext2_inode *inode, struct ext2_group_desc *group)
{
	void *block;

	struct ext2_dir_entry_2 *entry;
	unsigned int size = 0;

	if ((block = malloc(block_size)) == NULL)
	{
		fprintf(stderr, "Memory error\n");
		close(fd);
		exit(1);
	}

	lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
	read(fd, block, block_size);

	entry = (struct ext2_dir_entry_2 *)block;

	unsigned int offset_entry = 0;

	vetorEntradasDir[vetorEntradasDir.size() - 2]->rec_len = 8 + converteParaMult4(vetorEntradasDir[vetorEntradasDir.size() - 2]->name_len);

	for (int i = 0; i < vetorEntradasDir.size(); i++)
	{
		lseek(fd, BLOCK_OFFSET(inode->i_block[0]) + offset_entry, SEEK_SET);
		printf("\n%s, %hu\n", vetorEntradasDir[i]->name, vetorEntradasDir[i]->rec_len);
		write(fd, vetorEntradasDir[i], vetorEntradasDir[i]->rec_len);

		offset_entry += vetorEntradasDir[i]->rec_len;
	}

	vetorEntradasDir.clear();
	free(block);

	return;
}

void funct_rename(struct ext2_inode *inode, struct ext2_group_desc *group, char *nomeArquivo, char *novoNomeArquivo)
{
	// Agora temos outra ideia: vamos criar um vetor que conterá as entradas do diretório. A entrada que queremos
	// modificar removemos do vetor. Criamos uma entrada com o que queremos e colocamos no vetor, no final. Disto,
	// mandamos os elementos do vetor para o bloco[].

	int posArquivo;

	constroiListaDiretorios(inode, group, nomeArquivo, &posArquivo);

	removeAtualizaListaDiretorios(inode, group, novoNomeArquivo, nomeArquivo, posArquivo);

	atualizaListaReal(inode, group);
}

//
char *caminhoAtual(vector<string> caminhoVetor)
{
	char *caminho = (char *)calloc(100, sizeof(char));

	// if (caminhoVetor.empty())
	//{
	//	strcat(caminho, "/");
	// }

	for (long unsigned int i = 0; i < caminhoVetor.size(); i++)
	{
		strcat(caminho, "/");

		strcat(caminho, caminhoVetor[i].c_str());
	}

	return caminho;
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
		char *caminhoPwd;
		caminhoPwd = caminhoAtual(vetorCaminhoAtual);

		printf("\n%s\n", caminhoPwd);
	}
	else if (!strcmp(comandoPrincipal, "rename"))
	{
		funct_rename(inode, group, comandoInteiro[1], comandoInteiro[2]);
	}
	else if (!strcmp(comandoPrincipal, "cp"))
	{
		funct_cp(inode, group, comandoInteiro[1], &grupoAtual, comandoInteiro[2]);
	}
	else if (!strcmp(comandoPrincipal, "mkdir"))
	{
		funct_mkdir(inode, group, comandoInteiro[1], grupoAtual);
	}
	else if (!strcmp(comandoPrincipal, "touch"))
	{
		funct_touch(inode, group, comandoInteiro[1], grupoAtual);
	}
	else if (!strcmp(comandoPrincipal, "rm"))
	{
		funct_rm(inode, group, comandoInteiro[1], grupoAtual);
	}
	else if (!strcmp(comandoPrincipal, "rmdir"))
	{
		funct_rmdir(inode, group, comandoInteiro[1], grupoAtual);
	}
	else
	{
		printf("Erro: Comando não suportado\n");
	}

	return 0;
}

int main(void)
{
	struct ext2_group_desc group;
	struct ext2_inode inode;

	char *entrada = (char *)malloc(100 * sizeof(char));		 // Comando enviado pelo terminal
	char **argumentos = (char **)malloc(3 * sizeof(char *)); // Lista de strings/argumentos do comando
	char *token;											 // Cada parte do comando;
	int indexArgumentos = 0;								 // Número de partes do comando
	int numeroArgumentos = 0;

	init_super(&group, &inode);

	while (1)
	{
		indexArgumentos = 0;
		numeroArgumentos = 0;

		char prompt[100] = "";

		strcat(prompt, "[");

		char *caminhoAbsoluto;
		caminhoAbsoluto = caminhoAtual(vetorCaminhoAtual);

		strcat(prompt, caminhoAbsoluto);

		strcat(prompt, "/]$> ");

		entrada = readline(prompt);

		free(caminhoAbsoluto);

		entrada[strcspn(entrada, "\n")] = 0; // Consome o '\n' que o readline coloca;

		if (!strcmp(entrada, "")) // Reinicia o processo de entrada se nenhum comando for digitado;
		{
			continue;
		}

		// entrada[strcspn(entrada, "\n")] = 0; // Consome o '\n' que o readline coloca;
		add_history(entrada); // Acrescenta o comando no histórico;

		token = strtok(entrada, " ");
		if (!(strcasecmp(token, "exit"))) // Sai quando for digitado exit;
		{
			return 0;
		}

		argumentos[indexArgumentos] = (char *)malloc(50 * sizeof(char));
		argumentos[indexArgumentos] = token;
		;

		numeroArgumentos++;

		token = strtok(NULL, " ");

		while (token != NULL)
		{
			indexArgumentos++;
			numeroArgumentos++;

			argumentos[indexArgumentos] = (char *)malloc(50 * sizeof(char));
			argumentos[indexArgumentos] = token;

			token = strtok(NULL, " ");
		}

		if (executarComando(argumentos[0], argumentos, &inode, &group) == -1)
		{
			printf("\nErro\n");
			exit(1);
		}

		free(argumentos[0]);
	}

	exit(0);
}