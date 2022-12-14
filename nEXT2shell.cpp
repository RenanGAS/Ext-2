/**
 * Descrição: Este código implementa um shell que consegue realizar leituras e manipulações em uma imagem
 * formatada para o sistema de arquivos EXT2.
 *
 * Autor: Christofer Daniel Rodrigues Santos, Guilherme Augusto Rodrigues Maturana, Renan Guensuke Aoki Sakashita
 * Data de criação: 22/10/2022
 * Datas de atualização: 04/11/2022, 11/11/2022, 18/11/2022, 25/11/2022, 02/12/2022, 15/12/2022, 16/12/2022, 17/12/2022, 18/12/2022
 */


#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/types.h>
#include "nEXT2shell.h"
#include <string.h>
#include <time.h>
#include <fstream>
#include <readline/readline.h>
#include <readline/history.h>
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

#define EXT2_S_IRUSR 0x0100 // Usuário: read
#define EXT2_S_IWUSR 0x0080 // Usuário: write
#define EXT2_S_IXUSR 0x0040 // Usuário: execute
#define EXT2_S_IRGRP 0x0020 // Grupo: read
#define EXT2_S_IWGRP 0x0010 // Grupo: write
#define EXT2_S_IXGRP 0x0008 // Grupo: execute
#define EXT2_S_IROTH 0x0004 // Outros: read
#define EXT2_S_IWOTH 0x0002 // Outros: write
#define EXT2_S_IXOTH 0x0001 // Outros: execute

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
		{
			fprintf(stderr, "\nmemory insufficient.\n");
			close(fd);
			exit(1);
		}

		lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
		read(fd, block, block_size);

		entry = (struct ext2_dir_entry_2 *)block;

		while ((size < inode->i_size) && entry->inode)
		{
			char file_name[EXT2_NAME_LEN + 1];
			memcpy(file_name, entry->name, entry->name_len);
			file_name[entry->name_len] = 0;

			if (!strcmp(nome, file_name))
			{
				*valorInode = entry->inode;
				break;
			}

			entry = (ext2_dir_entry_2 *)((char *)entry + entry->rec_len);
			size += entry->rec_len;
		}

		free(block);
	}
}

// Retorna a última posição da lista de arquivos de um diretório
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
			fprintf(stderr, "\nmemory insufficient.\n");
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

			entry = (ext2_dir_entry_2 *)((char *)entry + entry->rec_len);
			size += entry->rec_len;
		}

		free(block);
	}
	return acc;
}

/* Escreve o inode 'inode' de número 'inode_no' na Tabela de Inodes de 'group'

inode_no: número do Inode 'inode' a ser escrito
group: Grupo em que 'inode' será escrito
inode: Inode a ser escrito
*/
void write_inode(unsigned int inode_no, struct ext2_group_desc *group, struct ext2_inode *inode)
{
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
		fprintf(stderr, "\nmemory insufficient.\n");
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
				printf("\nnot a directory.\n");
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
		printf("\ndirectory not found.\n");
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
		if (i < EXT2_NDIR_BLOCKS)
			printf("Block %2u : %u\n", i, inode->i_block[i]);
		else if (i == EXT2_IND_BLOCK)
			printf("Single   : %u\n", inode->i_block[i]);
		else if (i == EXT2_DIND_BLOCK)
			printf("Double   : %u\n", inode->i_block[i]);
		else if (i == EXT2_TIND_BLOCK)
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
		fprintf(stderr, "not a Ext2 filesystem.\n");
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

	// Atualização do Inode para o que contém o arquivo 'nome'
	read_dir(novoInode, novoGroup, &valorInodeTmp, nome);

	if (valorInodeTmp == -1)
	{
		return -1;
	}
	else if (valorInodeTmp == -2)
	{
		return -2;
	}

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

	// Exemplo:
	// a = 10110011
	// j = 4
	// a >> j = 00001011
	// 00001011 & 00000001 = 00000001
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

	free(bitmap);

	return 0;
}

// Retorna o offset do primeiro Bloco livre no bitmap de Blocos
int find_free_block(struct ext2_group_desc *group)
{
	unsigned char *bitmap;

	int buscar = 1;

	bitmap = (unsigned char *)malloc(block_size);
	lseek(fd, BLOCK_OFFSET(group->bg_block_bitmap), SEEK_SET);
	read(fd, bitmap, block_size);

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

	bitmap = (char *)malloc(block_size);

	// Lê o bitmap de blocos
	lseek(fd, BLOCK_OFFSET(group->bg_block_bitmap), SEEK_SET);
	read(fd, bitmap, block_size);

	// Pega o byte correspondente de bitVal
	char tmp = bitmap[y];

	tmp = (tmp | marcado); // Constrói o valor do byte de 'bitVal' que teríamos se estivesse 'ocupado'
	bitmap[y] = tmp;	   // Armazena no bitmap

	// Atualiza o bitmap
	lseek(fd, BLOCK_OFFSET(group->bg_block_bitmap), SEEK_SET);
	write(fd, bitmap, block_size);

	free(bitmap);
}

// Marca a posição bitVal no bitmap de Inodes como ocupada
void set_inode_bitmap(struct ext2_group_desc *group, int bitVal)
{
	char *bitmap;
	int y = bitVal / 8; // Pega o byte em que se encontra o Inode
	int x = bitVal % 8; // Pega o offset no byte

	int marcado = (0x1 << x);

	bitmap = (char *)malloc(block_size);

	lseek(fd, BLOCK_OFFSET(group->bg_inode_bitmap), SEEK_SET);
	read(fd, bitmap, block_size);

	char tmp = bitmap[y];

	tmp = (tmp | marcado);
	bitmap[y] = tmp;

	lseek(fd, BLOCK_OFFSET(group->bg_inode_bitmap), SEEK_SET);
	write(fd, bitmap, block_size);

	free(bitmap);
}

// Cacula o número a ser somado ao tamanho do nome do arquivo para que a entrada tenha tamanho múltiplo de 4
int roundLen(int tamanho)
{
	return (tamanho % 4 == 0) ? 0 : 4 - (tamanho % 4);
}

/* Atualiza o valor de 'group' com base em 'groupNum', e o valor de 'super'

Utilizado nas funções: touch, mkdir, rm e rmdir
*/
void rewriteSuperAndGroup(struct ext2_group_desc *group, int groupNum)
{
	lseek(fd, BASE_OFFSET + block_size + sizeof(struct ext2_group_desc) * groupNum, SEEK_SET);
	write(fd, group, sizeof(struct ext2_group_desc));

	lseek(fd, BASE_OFFSET, SEEK_SET);
	write(fd, &super, sizeof(struct ext2_super_block));
}

/* Cria um diretório de nome 'nome' no diretório atual

nome: nome do diretório que se deseja criar
numGrupo: valor de grupoAtual
*/
void funct_mkdir(struct ext2_inode *inode, struct ext2_group_desc *group, char *nome, int numGrupo)
{
	struct ext2_dir_entry_2 *entryTmp = (struct ext2_dir_entry_2 *)malloc(sizeof(struct ext2_dir_entry_2));
	struct ext2_inode *inodeTemp = (struct ext2_inode *)malloc(sizeof(struct ext2_inode));
	struct ext2_group_desc *groupDest = (struct ext2_group_desc *)malloc(sizeof(struct ext2_group_desc)); // Grupo 0

	// Lê em groupDest, o grupo 0
	lseek(fd, BASE_OFFSET + block_size + sizeof(struct ext2_group_desc) * 0, SEEK_SET);
	read(fd, groupDest, sizeof(struct ext2_group_desc));

	void *producedBlock;
	struct ext2_dir_entry_2 *producedEntry;

	char *nomeFinal;
	int tamNome;
	int arredondamento = 0;
	int tamanho = 0;
	int inodeVal = 0;	 // Offset do primeiro Inode livre no bitmap de Inodes do grupo 0
	long existe = 0;	 // Variável para validação de 'nome'
	int blockVal = 0;	 // Offset do primeiro Bloco livre no bitmap de Blocos do grupo 0
	long inodeAtual = 0; // Número do Inode do diretório corrente

	// Verifica se 'nome' é nome de alguma entrada do diretório corrente
	read_dir(inode, group, &existe, nome);

	if (existe != -1)
	{
		printf("\nfile already exists.\n");
		return;
	}

	// Atribui para inodeAtual o número do Inode do diretório corrente
	read_dir(inode, group, &inodeAtual, ".");

	inodeVal = find_free_inode(groupDest) + 1;

	set_inode_bitmap(groupDest, (inodeVal - 1));

	// Tratamento do nome do diretório novo
	tamNome = strlen(nome);
	arredondamento = roundLen(8 + tamNome);
	nomeFinal = (char *)malloc((tamNome + arredondamento) * sizeof(char));
	strcpy(nomeFinal, nome);

	for (int i = 0; i < arredondamento; i++)
	{
		strcat(nomeFinal, "\0");
	}

	// Criação de uma entrada do tipo diretório

	// Entrada tem o tamanho de um bloco
	producedBlock = malloc(block_size);
	producedEntry = (struct ext2_dir_entry_2 *)producedBlock;

	// Na posição 0 contém a entrada '.'
	producedEntry = (ext2_dir_entry_2 *)((char *)producedEntry + 0);
	producedEntry->file_type = 2;
	producedEntry->name_len = 1;
	producedEntry->rec_len = 12;
	memcpy(producedEntry->name, ".\0\0\0", 4);
	producedEntry->inode = inodeVal; // Referencia o Inode identificado como vazio no bitmap

	// Na posição 12 contém a entrada '..'
	producedEntry = (ext2_dir_entry_2 *)((char *)producedEntry + producedEntry->rec_len);
	producedEntry->file_type = 2;
	producedEntry->name_len = 2;
	producedEntry->rec_len = 1012;
	memcpy(producedEntry->name, "..\0\0", 4);
	producedEntry->inode = inodeAtual; // Referencia o Inode do diretório pai

	blockVal = find_free_block(groupDest) + 1;

	set_block_bitmap(groupDest, (blockVal - 1));

	// Escreve o bloco producedBlock que contém as entradas '.' e '..' criadas, no primeiro bloco vazio do grupo 0
	lseek(fd, BLOCK_OFFSET(blockVal), SEEK_SET);
	write(fd, producedBlock, block_size);

	int temp = getLastEntry(inode, group);

	void *block;

	if (S_ISDIR(inode->i_mode))
	{
		struct ext2_dir_entry_2 *entry;

		unsigned int size = 0;

		if ((block = malloc(block_size)) == NULL)
		{
			fprintf(stderr, "\nmemory insufficient.\n");
			close(fd);
			exit(1);
		}

		// Lê em block, o bloco 0 do diretório atual com suas entradas
		lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
		read(fd, block, block_size);

		entry = (struct ext2_dir_entry_2 *)block;

		// Atribui a 'entry' a última posição da lista de entries
		entry = (ext2_dir_entry_2 *)((char *)entry + temp);

		// Ajusta re_len da última entrada para um valor 'normal'
		// Agora a última entrada será o diretório novo
		entry->rec_len = 8 + entry->name_len;
		entry->rec_len = entry->rec_len + roundLen(entry->rec_len);

		// Criação do Inode do diretório novo
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

		write_inode(inodeVal, groupDest, inodeTemp);

		// Adição da nova entrada no fim lista de entradas

		// Cálculo da nova última posição
		temp = temp + entry->rec_len;

		// Atribui a 'entry' a nova última posição da lista de entries
		entry = (ext2_dir_entry_2 *)((char *)entry + entry->rec_len);

		// Definição de valores

		entry->inode = inodeVal;
		entry->name_len = tamNome;
		entry->rec_len = 1024 - temp; // Ajusta o valor de rec_len da nova última posição

		memcpy(entry->name, nomeFinal, tamNome * sizeof(char));

		entry->file_type = 2;

		// Sobreescrita do bloco antigo, com o bloco com a nova entrada
		lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
		write(fd, block, block_size);
	}

	// Atualização do número de Blocos livres em 'group' e 'super'
	group->bg_free_blocks_count = group->bg_free_blocks_count - 1;
	super.s_free_blocks_count = super.s_free_blocks_count - 1;

	// Atualização do número de Inodes livres em 'group' e 'super'
	group->bg_free_inodes_count = group->bg_free_inodes_count - 1;
	super.s_free_inodes_count = super.s_free_inodes_count - 1;

	// Escrita dos novos valores
	rewriteSuperAndGroup(group, numGrupo);

	free(inodeTemp);
	free(entryTmp);
}

/* Cria um arquivo com nome 'nome'

grupoAtual: variável global que indica o grupo corrente
*/
void funct_touch(struct ext2_inode *inode, struct ext2_group_desc *group, char *nome, int grupoAtual)
{
	struct ext2_dir_entry_2 *entryTmp = (struct ext2_dir_entry_2 *)malloc(sizeof(struct ext2_dir_entry_2));
	struct ext2_inode *inodeTemp = (struct ext2_inode *)malloc(sizeof(struct ext2_inode));
	struct ext2_group_desc *groupDest = (struct ext2_group_desc *)malloc(sizeof(struct ext2_group_desc)); // Grupo em que será criado o arquivo

	// Lê em groupDest o grupo 0
	lseek(fd, BASE_OFFSET + block_size + sizeof(struct ext2_group_desc) * 0, SEEK_SET);
	read(fd, groupDest, sizeof(struct ext2_group_desc));

	char *nomeFinal;
	int tamNome;
	int arredondamento = 0;
	int tamanho = 0;
	int inodeVal = 0;			// Offset do primeiro Inode livre no bitmap de Inodes do grupo 0
	long existe = 0;			// Variável para validação de 'nome' 
	int grupoTmp = -1; 

	// Verifica se 'nome' é nome de alguma entrada do diretório corrente
	read_dir(inode, group, &existe, nome);

	if (existe != -1)
	{
		printf("\nfile already exists.\n");
		return;
	}

	inodeVal = find_free_inode(group) + 1;

	set_inode_bitmap(group, (inodeVal - 1));

	// Tratamento do nome do diretório novo

	tamNome = strlen(nome);
	arredondamento = roundLen(8 + tamNome);
	nomeFinal = (char *)malloc((tamNome + arredondamento) * sizeof(char));
	strcpy(nomeFinal, nome);

	for (int i = 0; i < arredondamento; i++)
	{
		strcat(nomeFinal, "\0");
	}

	int temp = getLastEntry(inode, group);

	tamNome = strlen(nome);

	void *block;

	if (S_ISDIR(inode->i_mode))
	{
		struct ext2_dir_entry_2 *entry;

		unsigned int size = 0;

		if ((block = malloc(block_size)) == NULL)
		{ 
			fprintf(stderr, "\nmemory insufficient.\n");
			close(fd);
			exit(1);
		}

		// Lê em block, o bloco 0 do diretório atual com suas entradas
		lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
		read(fd, block, block_size);

		entry = (struct ext2_dir_entry_2 *)block;

		// Atribui a 'entry' a última posição da lista de entries
		entry = (ext2_dir_entry_2 *)((char *)entry + temp);

		// Ajusta re_len da última entrada para um valor 'normal'
		// Agora a última entrada será o arquivo novo
		entry->rec_len = 8 + entry->name_len;
		entry->rec_len = entry->rec_len + roundLen(entry->rec_len);

		// Criação do Inode do arquivo novo
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

		write_inode(inodeVal, groupDest, inodeTemp);

		// Adição da nova entrada no fim lista de entradas

		// Cálculo da nova última posição
		temp = temp + entry->rec_len;

		// Atribui a 'entry' a nova última posição da lista de entries
		entry = (ext2_dir_entry_2 *)((char *)entry + entry->rec_len);

		// Definição de valores

		entry->inode = inodeVal;
		entry->name_len = tamNome;
		entry->rec_len = 1024 - temp; // Ajusta o valor de rec_len da nova última posição

		memcpy(entry->name, nomeFinal, tamNome * sizeof(char));

		entry->file_type = 1;

		// Sobreescrita do bloco antigo, com o bloco com a nova entrada
		lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
		write(fd, block, block_size);
	}

	// Atualização do número de Inodes livres em 'group' e 'super'
	group->bg_free_inodes_count = group->bg_free_inodes_count - 1;
	super.s_free_inodes_count = super.s_free_inodes_count - 1;

	// Escrita dos novos valores
	rewriteSuperAndGroup(groupDest, 0);

	free(inodeTemp);
	free(entryTmp);
}

// Retorna quantas entradas o diretório de inode 'inode' possui. Desconta as entradas '.' e '..'
int isLoaded(struct ext2_inode *inode, struct ext2_group_desc *group)
{
	int contador = 0;

	void *block;

	if (S_ISDIR(inode->i_mode))
	{
		struct ext2_dir_entry_2 *entry;
		unsigned int size = 0;

		if ((block = malloc(block_size)) == NULL)
		{ 
			fprintf(stderr, "\nmemory insufficient.\n");
			close(fd);
			exit(1);
		}

		lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
		read(fd, block, block_size); 

		entry = (struct ext2_dir_entry_2 *)block; 
												  
		while ((size < inode->i_size) && entry->inode)
		{
			entry = (ext2_dir_entry_2 *)((char *)entry + entry->rec_len);
			size += entry->rec_len;
			contador++;
		}
		
		free(block);
		
		return (contador - 2);
	}
	return -1;
}

/* Se o bloco passado em 'valor' não pertence ao grupo 'grupoAtual', troca o grupo para o correspondente do bloco

valor: número do bloco
grupoAtual: variável global que indica o grupo corrente
*/
void trocaGrupoBlock(int valor, struct ext2_group_desc *group, int *grupoAtual)
{
	// Calcula-se o grupo de um bloco sabendo da quantidade de blocos por grupo
	unsigned int block_group = (valor - 1) / super.s_blocks_per_group;

	if (block_group != (*grupoAtual))
	{
		*grupoAtual = block_group;

		lseek(fd, BASE_OFFSET + block_size + sizeof(struct ext2_group_desc) * block_group, SEEK_SET);
		read(fd, group, sizeof(struct ext2_group_desc));
	}
}

/* Marca o bloco de número 'bitVal' como desocupado no bitmap de blocos de 'group'

Utilizado nas funções rm e rmdir
bitVal: número do bloco a ser desmarcado no bitmap de blocos
*/
void unset_block_bitmap(struct ext2_group_desc *group, int bitVal)
{
	char *bitmap;

	int y = (bitVal) / 8; // Pega o byte em que se encontra o Bloco
	int x = (bitVal) % 8; // Pega o offset no byte

	// Exemplo:
	// bitVal = 10110011 = 179
	// y = 22
	// x = 3
	// marcado = 11111110 << 3 = 11110000
	// marcado = 11110000 | (11111111 >> 5) = 11110000 | 00000111 = 11110111

	int marcado = (0xFE << x);

	marcado = marcado | (0xFF >> (8 - x));

	bitmap = (char *)malloc(block_size); 

	// Lê o bitmap do grupo 'group' em bitmap
	lseek(fd, BLOCK_OFFSET(group->bg_block_bitmap), SEEK_SET);
	read(fd, bitmap, block_size);

	char tmp = bitmap[y];
	
	tmp = (tmp & marcado);

	bitmap[y] = tmp;

	// Reescreve o bitmap com o bloco desmarcado
	lseek(fd, BLOCK_OFFSET(group->bg_block_bitmap), SEEK_SET);
	write(fd, bitmap, block_size);

	free(bitmap);
}

/* Remove a entrada de nome 'nome' da lista de entradas do diretório de inode 'inode'

Utilizada nas funções rm e rmdir
nome: nome da entrada a ser removida
*/
void removeEntry(struct ext2_inode *inode, struct ext2_group_desc *group, char *nome)
{
	void *block;
	void *newBlock;
	int acc = 0;				// Soma o rec_len de todas as entradas
	int acc2 = 0;				// Soma o rec_len de todas entradas menos da entrada a ser removida
	int lastEntry = 0;			// Última posição da lista de entradas
	int removedSize = 0;		// rec_len da entrada removida
	int lastEntrySize = 0;		// rec_len da última entrada do diretório corrente
	int ultimoTam = 0;			// rec_len da última entrada (não removida)
	int removido = 0;			// Indica que uma entrada foi removida

	lastEntry = getLastEntry(inode, group);

	if (S_ISDIR(inode->i_mode))
	{
		struct ext2_dir_entry_2 *entry;
		struct ext2_dir_entry_2 *newEntry;
		unsigned int size = 0;

		if ((block = malloc(block_size)) == NULL)
		{
			fprintf(stderr, "\ninsufficient memory.\n");
			close(fd);
			exit(1);
		}

		if ((newBlock = malloc(block_size)) == NULL)
		{
			fprintf(stderr, "\ninsufficient memory.\n");
			close(fd);
			exit(1);
		}

		lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
		read(fd, block, block_size); 

		entry = (struct ext2_dir_entry_2 *)block; 

		newEntry = (struct ext2_dir_entry_2 *)newBlock;

		while ((size < inode->i_size) && entry->inode)
		{
			// Se o acumulador geral se torna diferente do acumulador 2, significa que 'removemos' uma entrada
			// Não a colocamos em 'newBlock'
			if (acc != acc2)
			{
				removido = 1;
			}
			
			char file_name[EXT2_NAME_LEN + 1];
			memcpy(file_name, entry->name, entry->name_len);
			file_name[entry->name_len] = 0; 

			// Se o nome da entrada não é igual a da que desejamos remover, a adicionamos em 'newBlock'
			if (strcmp(file_name, nome) != 0)
			{
				newEntry->file_type = entry->file_type;
				newEntry->inode = entry->inode;
				newEntry->name_len = entry->name_len;
				newEntry->rec_len = entry->rec_len;

				memcpy(newEntry->name, entry->name, entry->name_len + roundLen(8 + entry->name_len));

				ultimoTam = newEntry->rec_len;

				acc2 += newEntry->rec_len;

				newEntry = (ext2_dir_entry_2 *)((char *)newEntry + newEntry->rec_len);
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
				lastEntrySize = entry->rec_len;
			}
		}

		// Reescreve o bloco com a lista de entradas com o conteúdo de 'newBlock'
		lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
		write(fd, newBlock, block_size); 

		// Pega a nova posição da última entrada
		lastEntry = getLastEntry(inode, group);

		lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
		read(fd, newBlock, block_size);

		newEntry = (struct ext2_dir_entry_2 *)newBlock;

		// Se foi removido uma entrada que não era a última, temos que ajustar o rec_len da nova última entrada, 
		// fazendo 1024 menos a soma do rec_len das entradas anteriores
		if (lastEntrySize == removedSize)
		{
			newEntry = (ext2_dir_entry_2 *)((char *)newEntry + (acc2 - ultimoTam));
			newEntry->rec_len = 1024 - acc2 + ultimoTam;
		}
		else // Se não, recebe a rec_len da entrada que foi removida
		{
			newEntry = (ext2_dir_entry_2 *)((char *)newEntry + (acc2 - ultimoTam));
			newEntry->rec_len = newEntry->rec_len + removedSize;
		}

		lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
		write(fd, newBlock, block_size);
		
		free(block);
	}
}

/* Marca o inode de número 'bitVal' como desocupado no bitmap de Inodes de 'group'

Utilizado nas funções rm e rmdir
bitVal: número do inode a ser desmarcado no bitmap de Inodes de 'group'
*/
void unset_inode_bitmap(struct ext2_group_desc *group, int bitVal)
{
	char *bitmap;

	int y = (bitVal) / 8; // Pega o byte em que se encontra o Inode
	int x = (bitVal) % 8; // Pega o offset no byte

	// Exemplo:
	// bitVal = 10110011 = 179
	// y = 22
	// x = 3
	// marcado = 11111110 << 3 = 11110000
	// marcado = 11110000 | (11111111 >> 5) = 11110000 | 00000111 = 11110111

	int marcado = (0xFE << x);

	marcado = marcado | (0xFF >> (8 - x));

	bitmap = (char *)malloc(block_size);

	lseek(fd, BLOCK_OFFSET(group->bg_inode_bitmap), SEEK_SET);
	read(fd, bitmap, block_size);

	char tmp = bitmap[y];

	tmp = (tmp & marcado);

	bitmap[y] = tmp;

	// Reescreve o bitmap com o bitmap no qual foi desmarcado o Inode
	lseek(fd, BLOCK_OFFSET(group->bg_inode_bitmap), SEEK_SET);
	write(fd, bitmap, block_size);

	// Atualiza o número de Inodes livres
	group->bg_free_inodes_count = group->bg_free_inodes_count + 1;
	super.s_free_inodes_count = super.s_free_inodes_count + 1;

	free(bitmap);
}

/* Remove o diretório de nome 'nome'

nome: nome do diretório a ser removido
grupoAtual: variável global que indica o grupo corrente
*/
void funct_rmdir(struct ext2_inode *inode, struct ext2_group_desc *group, char *nome, int grupoAtual)
{
	int numGrupo = grupoAtual;
	int numblocos = 0;
	long valorInodeTmp;

	struct ext2_group_desc *grupoTemp = (struct ext2_group_desc *)malloc(sizeof(struct ext2_group_desc));
	struct ext2_inode *inodeTemp = (struct ext2_inode *)malloc(sizeof(struct ext2_inode));
	memcpy(grupoTemp, group, sizeof(struct ext2_group_desc));
	memcpy(inodeTemp, inode, sizeof(struct ext2_inode));

	read_dir(inodeTemp, grupoTemp, &valorInodeTmp, nome);

	trocaGrupo(&valorInodeTmp, grupoTemp, &numGrupo);

	unsigned int index = valorInodeTmp % super.s_inodes_per_group;

	read_inode(index, grupoTemp, inodeTemp);

	numblocos = inodeTemp->i_blocks;

	if (valorInodeTmp == -1)
	{
		printf("\nfile not found.\n");
		return;
	}
	if (S_ISDIR(inodeTemp->i_mode) == 0)
	{
		printf("\nnot a directory.\n");
		return;
	}

	// Se não há entradas no diretório
	if (!isLoaded(inodeTemp, grupoTemp))
	{
		trocaGrupoBlock(inodeTemp->i_block[0], grupoTemp, &numGrupo); // Localiza e muda para o grupo correspondente do primeiro bloco do diretório
		unset_block_bitmap(grupoTemp, inodeTemp->i_block[0]); 		  // Marca o bloco como desocupado no bitmap de blocos do grupo correspondente
		removeEntry(inode, group, nome); 							  // Remove o diretório da lista de entradas do diretório pai
		unset_inode_bitmap(group, valorInodeTmp); 					  // Marca o Inode do diretório como desocupado no bitmap de Inodes do grupo correspondente
		rewriteSuperAndGroup(grupoTemp, grupoAtual);				  // Atualiza o número de Blocos e Inodes livres
	}
	else
	{
		printf("\ndirectory not empty.\n");
	}

	free(inodeTemp);
	free(grupoTemp);
}

/* Remove o arquivo de nome 'nome'

nome: nome do arquivo a ser removido
grupoAtual: variável global que indica o grupo corrente
*/
void funct_rm(struct ext2_inode *inode, struct ext2_group_desc *group, char *nome, int grupoAtual)
{
	int contador = 0;
	int numGrupo = grupoAtual;
	int numblocos = 0;
	int fileSize = 0;
	long valorInodeTmp;

	struct ext2_group_desc *grupoTemp = (struct ext2_group_desc *)malloc(sizeof(struct ext2_group_desc));
	struct ext2_inode *inodeTemp = (struct ext2_inode *)malloc(sizeof(struct ext2_inode));
	memcpy(grupoTemp, group, sizeof(struct ext2_group_desc));
	memcpy(inodeTemp, inode, sizeof(struct ext2_inode));

	read_dir(inodeTemp, grupoTemp, &valorInodeTmp, nome);

	if (valorInodeTmp == -1)
	{
		printf("\nfile not found.\n");
		return;
	}

	// Localiza o grupo do Inode do arquivo a ser removido
	unsigned int block_group = ((valorInodeTmp)-1) / super.s_inodes_per_group;

	lseek(fd, BASE_OFFSET + block_size + sizeof(struct ext2_group_desc) * block_group, SEEK_SET);
	read(fd, group, sizeof(struct ext2_group_desc));

	unsigned int index = valorInodeTmp % super.s_inodes_per_group;

	// Obtém a estrutura do Inode do arquivo a ser removido
	read_inode(index, grupoTemp, inodeTemp);

	numblocos = inodeTemp->i_blocks;

	if (S_ISDIR(inodeTemp->i_mode))
	{
		printf("\nnot a file.\n");
		return;
	}

	// Desmarca todos os blocos do Inode do arquivo e atualiza a contagem de blocos em 'super' e 'group'

	unsigned int *singleInd = (unsigned int *)malloc(sizeof(unsigned int) * 256);
	unsigned int *doubleInd = (unsigned int *)malloc(sizeof(unsigned int) * 256);
	
	fileSize = inodeTemp->i_size;

	for (int i = 0; i < 12 && fileSize > 0; i++)
	{
		trocaGrupoBlock(inodeTemp->i_block[i], grupoTemp, &numGrupo);
		unset_block_bitmap(grupoTemp, inodeTemp->i_block[i]);
		rewriteSuperAndGroup(grupoTemp, numGrupo);

		fileSize -= block_size;
		contador++;
	}

	lseek(fd, BLOCK_OFFSET(inodeTemp->i_block[12]), SEEK_SET);
	read(fd, singleInd, block_size);

	for (int i = 0; i < 256 && fileSize > 0; i++)
	{
		trocaGrupoBlock(singleInd[i], grupoTemp, &numGrupo);
		unset_block_bitmap(grupoTemp, singleInd[i]);
		rewriteSuperAndGroup(grupoTemp, numGrupo);

		fileSize -= block_size;
		contador++;
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
			contador++;
		}
	}

	removeEntry(inode, group, nome); 					// Remove a entrada correspondente ao arquivo removido da lista de entradas
	trocaGrupo(&valorInodeTmp, grupoTemp, &numGrupo);   // Garante que o Inode do arquivo removido pertence ao grupo corrente
	unset_inode_bitmap(grupoTemp, valorInodeTmp);	    // Marca o bit do Inode removido como desocupado no bitmap de Inodes do grupo correspondente
	rewriteSuperAndGroup(grupoTemp, grupoAtual);		// Atualiza o número de Inodes livres em 'super' e 'group'
	
	free(inodeTemp);
	free(grupoTemp);
}

/* Copia os dados do arquivo de nome 'nome' para o arquivo de caminho absoluto 'arquivoDest'

nome: nome do arquivo a ser copiado
arquivoDest: caminho absoluto do arquivo a ser escrito a cópia
*/
void funct_cp(struct ext2_inode *inode, struct ext2_group_desc *group, char *nome, int *grupoAtual, char *arquivoDest)
{
	struct ext2_group_desc *grupoTemp = (struct ext2_group_desc *)malloc(sizeof(struct ext2_group_desc));
	struct ext2_inode *inodeTemp = (struct ext2_inode *)malloc(sizeof(struct ext2_inode));

	int retorno = getArquivoPorNome(inode, group, nome, grupoAtual, inodeTemp, grupoTemp);
	if (retorno == -1)
	{
		printf("\nfile not found.\n");
		return;
	}

	copiaArquivo(inodeTemp, arquivoDest);

	free(grupoTemp);
	free(inodeTemp);
}

/* Exibe o conteúdo do arquivo de nome 'nome'

nome: nome do arquivo
*/
void funct_cat(struct ext2_inode *inode, struct ext2_group_desc *group, char *nome, int *grupoAtual)
{
	struct ext2_group_desc *grupoTemp = (struct ext2_group_desc *)malloc(sizeof(struct ext2_group_desc));
	struct ext2_inode *inodeTemp = (struct ext2_inode *)malloc(sizeof(struct ext2_inode));

	int retorno = getArquivoPorNome(inode, group, nome, grupoAtual, inodeTemp, grupoTemp);

	if (retorno == -1)
	{
		printf("\nfile not found.\n");
		return;
	}

	if (S_ISDIR(inodeTemp->i_mode))
	{
		printf("\nnot a file.\n");
		return;
	}

	printaArquivo(inodeTemp);

	free(grupoTemp);
	free(inodeTemp);
}

// Exibe informações do disco e do sistema de arquivos
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
		
		super.s_free_inodes_count,
		super.s_free_blocks_count,
		block_size,
		super.s_inode_size,
		(super.s_blocks_count / super.s_blocks_per_group), 
		
		super.s_blocks_per_group,
		super.s_inodes_per_group,
		(super.s_inodes_per_group / (block_size / sizeof(struct ext2_inode))));
}

// Exibe os atributos do arquivo ou diretório de nome 'nome'
void funct_attr(struct ext2_inode *inode, struct ext2_group_desc *group, char *nome, int grupoAtual)
{
	struct ext2_group_desc *grupoTemp = (struct ext2_group_desc *)malloc(sizeof(struct ext2_group_desc));
	struct ext2_inode *inodeTemp = (struct ext2_inode *)malloc(sizeof(struct ext2_inode));

	int retorno = getArquivoPorNome(inode, group, nome, &grupoAtual, inodeTemp, grupoTemp);

	if (retorno == -1)
	{
		printf("\nfile not found\n");
		return;
	}

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

	time_t tempo = (inodeTemp->i_mtime);

	struct tm *ptm = localtime(&tempo);

	printf("  %d/%d/%d %d:%d",
		   ptm->tm_mday, ptm->tm_mon + 1, (ptm->tm_year + 1900),
		   ptm->tm_hour, ptm->tm_min);
	printf("\n");

	free(inodeTemp);
	free(grupoTemp);
}

// Altera o diretório corrente para o diretório de nome 'nome'
void funct_cd(struct ext2_inode *inode, struct ext2_group_desc *group, int *grupoAtual, char *nome)
{
	long int inodeTmp = 0;

	constroiCaminho(inode, group, &inodeTmp, nome);

	trocaGrupo(&inodeTmp, group, grupoAtual);

	unsigned int index = ((int)inodeTmp) % super.s_inodes_per_group;

	read_inode(index, group, inode);
}

// Lista os arquivos e diretórios do diretório corrente
void funct_ls(struct ext2_inode *inode, struct ext2_group_desc *group)
{
	void *block;
	if (S_ISDIR(inode->i_mode))
	{
		struct ext2_dir_entry_2 *entry;
		unsigned int size = 0;

		if ((block = malloc(block_size)) == NULL)
		{ 
			fprintf(stderr, "\ninsufficient memory\n");
			close(fd);
			exit(1);
		}

		// Lista de entradas localizadas no primeiro bloco
		lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
		read(fd, block, block_size); 

		entry = (struct ext2_dir_entry_2 *)block; 

		while ((size < inode->i_size) && entry->inode)
		{
			char file_name[EXT2_NAME_LEN + 1];
			memcpy(file_name, entry->name, entry->name_len);
			file_name[entry->name_len] = 0; 

			printf("%s\n", file_name);
			printf("inode: %u\n", entry->inode);
			printf("record length: %u\n", entry->rec_len);
			printf("name length: %u\n", entry->name_len);
			printf("file type: %u\n", entry->file_type);
			printf("\n");
			
			entry = (ext2_dir_entry_2 *)((char *)entry + entry->rec_len);
			size += entry->rec_len;
		}

		free(block);
	}
}

/* Preenche 'vetorEntradasDir' com as entradas do diretório corrente e localiza a posição do arquivo a ser renomeado

nomeArquivo: nome do arquivo a ser renomeado
posArquivo: posição do arquivo a ser renomeado em 'vetorEntradasDir'
*/
void constroiListaDiretorios(struct ext2_inode *inode, struct ext2_group_desc *group, char *nomeArquivo, int *posArquivo)
{
	void *block;

	struct ext2_dir_entry_2 *entry;
	unsigned int size = 0;

	if ((block = malloc(block_size)) == NULL)
	{ 
		fprintf(stderr, "\ninsufficient memory.\n");
		close(fd);
		exit(1);
	}

	lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
	read(fd, block, block_size); 

	entry = (struct ext2_dir_entry_2 *)block;

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

/* Faz com que 'tamanho' seja múltiplo de 4, incrementando seu valor para o próximo múltiplo ou deixando-o com seu valor original  

tamanho: comprimento do nome do arquivo
*/
unsigned int converteParaMult4(unsigned int tamanho)
{
	int qtdDe4 = tamanho / 4;
	int emTermosde4 = qtdDe4 * 4;

	return (tamanho - (emTermosde4)) > 0 ? (emTermosde4 + 4) : emTermosde4;
}

/* Remove o arquivo a ser renomeado de 'vetorEntradasDir', ajusta o 'rec_len' do último elemento e do novo último elemento (arquivo renomeado), e o coloca em 'vetorEntradasDir'

novoNomeArquivo: novo nome para o arquivo de nome 'nomeArquivo'
nomeArquivo: nome do arquivo a ser renomeado
pos: posição do arquivo a ser renomeado em 'vetorEntradasDir'
*/
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

// Escreve as entradas em 'vetorEntradasDir' sobre as entradas antigas na lista de entradas do diretório corrente
void atualizaListaReal(struct ext2_inode *inode, struct ext2_group_desc *group)
{
	void *block;

	struct ext2_dir_entry_2 *entry;
	unsigned int size = 0;

	if ((block = malloc(block_size)) == NULL)
	{
		fprintf(stderr, "\ninsufficient memory.\n");
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
		write(fd, vetorEntradasDir[i], vetorEntradasDir[i]->rec_len);

		offset_entry += vetorEntradasDir[i]->rec_len;
	}

	vetorEntradasDir.clear();
	free(block);

	return;
}

// Renomeia o arquivo de nome 'nomeArquivo' para 'novoNomeArquivo'
void funct_rename(struct ext2_inode *inode, struct ext2_group_desc *group, char *nomeArquivo, char *novoNomeArquivo)
{
	int posArquivo;

	constroiListaDiretorios(inode, group, nomeArquivo, &posArquivo); // Preenche vetor auxiliar com as entradas do diretório e localiza o arquivo a ser renomeado

	removeAtualizaListaDiretorios(inode, group, novoNomeArquivo, nomeArquivo, posArquivo); // Cria uma nova entrada com o novo nome, remove a entrada antiga, e adiciona
																						   // a nova entrada no fim do vetor

	atualizaListaReal(inode, group); // Atualiza as entradas do diretório corrente com as entradas do vetor auxiliar
}

// Retorna o caminho armazenado em 'caminhoVetor'
char *caminhoAtual(vector<string> caminhoVetor)
{
	char *caminho = (char *)calloc(100, sizeof(char));

	if (caminhoVetor.empty()) // Não armazenamos 'root' em 'caminhoVetor'
	{
		strcat(caminho, "/");
	}

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
int executarComando(char *comandoPrincipal, int num_argumentos, char **comandoInteiro, struct ext2_inode *inode, struct ext2_group_desc *group)
{
	if (!strcmp(comandoPrincipal, "info"))
	{
		if (num_argumentos != 1)
		{
			printf("\ninvalid sintax.\n");
			return 1;
		}
		funct_info();
	}
	else if (!strcmp(comandoPrincipal, "cat"))
	{
		if (num_argumentos != 2)
		{
			printf("\ninvalid sintax.\n");
			return 1;
		}
		funct_cat(inode, group, comandoInteiro[1], &grupoAtual);
	}
	else if (!strcmp(comandoPrincipal, "attr"))
	{
		if (num_argumentos != 2)
		{
			printf("\ninvalid sintax.\n");
			return 1;
		}
		funct_attr(inode, group, comandoInteiro[1], grupoAtual);
	}
	else if (!strcmp(comandoPrincipal, "cd"))
	{
		if (num_argumentos != 2)
		{
			printf("\ninvalid sintax.\n");
			return 1;
		}
		funct_cd(inode, group, &grupoAtual, comandoInteiro[1]);
	}
	else if (!strcmp(comandoPrincipal, "ls"))
	{
		if (num_argumentos != 1)
		{
			printf("\ninvalid sintax.\n");
			return 1;
		}
		funct_ls(inode, group);
	}
	else if (!strcmp(comandoPrincipal, "pwd"))
	{
		if (num_argumentos != 1)
		{
			printf("\ninvalid sintax.\n");
			return 1;
		}
		char *caminhoPwd;
		caminhoPwd = caminhoAtual(vetorCaminhoAtual);

		printf("\n%s\n", caminhoPwd);
	}
	else if (!strcmp(comandoPrincipal, "rename"))
	{
		if (num_argumentos != 3)
		{
			printf("\ninvalid sintax.\n");
			return 1;
		}
		funct_rename(inode, group, comandoInteiro[1], comandoInteiro[2]);
	}
	else if (!strcmp(comandoPrincipal, "cp"))
	{
		if (num_argumentos != 3)
		{
			printf("\ninvalid sintax.\n");
			return 1;
		}
		funct_cp(inode, group, comandoInteiro[1], &grupoAtual, comandoInteiro[2]);
	}
	else if (!strcmp(comandoPrincipal, "mkdir"))
	{
		if (num_argumentos != 2)
		{
			printf("\ninvalid sintax.\n");
			return 1;
		}
		funct_mkdir(inode, group, comandoInteiro[1], grupoAtual);
	}
	else if (!strcmp(comandoPrincipal, "touch"))
	{
		if (num_argumentos != 2)
		{
			printf("\ninvalid sintax.\n");
			return 1;
		}
		funct_touch(inode, group, comandoInteiro[1], grupoAtual);
	}
	else if (!strcmp(comandoPrincipal, "rm"))
	{
		if (num_argumentos != 2)
		{
			printf("\ninvalid sintax.\n");
			return 1;
		}
		funct_rm(inode, group, comandoInteiro[1], grupoAtual);
	}
	else if (!strcmp(comandoPrincipal, "rmdir"))
	{
		if (num_argumentos != 2)
		{
			printf("\ninvalid sintax.\n");
			return 1;
		}
		funct_rmdir(inode, group, comandoInteiro[1], grupoAtual);
	}
	else
	{
		printf("\nunsupported command.\n");
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

		strcat(prompt, "]$> ");

		entrada = readline(prompt);

		free(caminhoAbsoluto);

		entrada[strcspn(entrada, "\n")] = 0; // Consome o '\n' que o readline coloca;

		if (!strcmp(entrada, "")) // Reinicia o processo de entrada se nenhum comando for digitado;
		{
			continue;
		}

		add_history(entrada); // Acrescenta o comando no histórico;

		token = strtok(entrada, " ");
		if (!(strcasecmp(token, "exit"))) // Sai quando for digitado exit;
		{
			return 0;
		}

		argumentos[indexArgumentos] = (char *)malloc(50 * sizeof(char));
		argumentos[indexArgumentos] = token;

		numeroArgumentos++;

		token = strtok(NULL, " ");

		while (token != NULL) // Identifica argumentos do comando
		{
			indexArgumentos++;
			numeroArgumentos++;

			argumentos[indexArgumentos] = (char *)malloc(50 * sizeof(char));
			argumentos[indexArgumentos] = token;

			token = strtok(NULL, " ");
		}

		int num_argumentos = indexArgumentos + 1;

		if (executarComando(argumentos[0], num_argumentos, argumentos, &inode, &group) == -1)
		{
			printf("\nunexpected behavior.\n");
			exit(1);
		}

		free(argumentos[0]);
	}

	exit(0);
}