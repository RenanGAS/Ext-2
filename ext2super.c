/*
 * ext2super.c
 *
 * Reads the super-block from a Ext2 floppy disk.
 *
 * Questions?
 * Emanuele Altieri
 * ealtieri@hampshire.edu
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

#define BASE_OFFSET 1024 /* locates beginning of the super block (first group) */
#define FD_DEVICE "./myext2image.img"
#define EXT2_SUPER_MAGIC 0xEF53 /* the floppy disk device */
#define BLOCK_OFFSET(block) (BASE_OFFSET + (block - 1) * block_size)

static unsigned int block_size = 0; /* block size (to be calculated) */
struct ext2_super_block super;

static void read_inode(int fd, unsigned int inode_no, const struct ext2_group_desc *group, struct ext2_inode *inode)
{
	printf("Dentro doreadinode %u\n", inode_no);
	lseek(fd, BLOCK_OFFSET(group->bg_inode_table) + (inode_no - 1) * sizeof(struct ext2_inode),
		  SEEK_SET);
	read(fd, inode, sizeof(struct ext2_inode));
} /* read_inode() */

static void read_dir(int fd, const struct ext2_inode *inode, const struct ext2_group_desc *group, unsigned int *valorInode, char *nome)
{
	void *block;
	char nometmp[256];
	char nomearq[256];
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
		memset(nomearq, 0, sizeof(nomearq));
		strcpy(nomearq, nome);
		while ((size < inode->i_size) && entry->inode)
		{
			char file_name[EXT2_NAME_LEN + 1];
			memcpy(file_name, entry->name, entry->name_len);
			file_name[entry->name_len] = 0; /* append null character to the file name */
			memset(nometmp, 0, sizeof(nometmp));
			strcpy(nometmp, "/");
			strcat(nometmp, file_name);
			// printf("%s\n", file_name);
			// print("%s", nomearq);
			// print("%s", nometmp);
			printf("%10u %s\n", entry->inode, file_name);
			if (!strcmp(nome, file_name))
			{
				// 	printf("ACHOU");
				printf("ACHOUS\n");
				*valorInode = entry->inode;
			}
			if (!strcmp(nometmp, nomearq))
			{
				printf("ACHOUS\n");
				*valorInode = entry->inode;
				break;
				// read_inode(fd, &valor, group, inode);
				// break;
			}
			// printf("%s", nometmp);
			entry = (void *)entry + entry->rec_len;
			size += entry->rec_len;
		}

		free(block);
	}
} /* read_dir() */

void trocaGrupo(int fd, unsigned int *valor, struct ext2_group_desc *group, int *grupoAtual)
{
	printf("\n--- TROCANDO O GRUPO ---\n");
	printf("INODE: %u\n", *valor);
	unsigned int block_group = ((*valor) - 1) / super.s_inodes_per_group;
	if (block_group != (*grupoAtual))
	{
		printf("trocou de grupo\n");
		*grupoAtual = block_group;
		printf("%d\n", block_group);
		lseek(fd, BASE_OFFSET + block_size + sizeof(struct ext2_group_desc) * block_group, SEEK_SET);
		read(fd, group, sizeof(struct ext2_group_desc));
	}
}

void funct_cd(int fd, struct ext2_inode *inode, struct ext2_group_desc *group, int *grupoAtual, char *nome)
{
	unsigned int inodeTmp = 0;
	read_dir(fd, inode, group, &inodeTmp, nome);
	printf("Inode:%u", inodeTmp);
	trocaGrupo(fd, &inodeTmp, group, grupoAtual);
	unsigned int index = ((int)inodeTmp) % super.s_inodes_per_group;
	read_inode(fd, index, group, inode);
}

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

void printInode(struct ext2_inode *inode)
{
	printf("Reading root inode\n"
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

void printaArquivo(int fd, struct ext2_inode *inode)
{
	char *buffer = malloc(sizeof(char) * block_size);
	printf("\n---Print do arquivo---\n");
	printInode(inode);
	lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
	read(fd, buffer, block_size);
	int sizeTemp = inode->i_size;
	int singleInd[256];
	int doubleInd[256];

	for (int i = 0; i < 12; i++)
	{
		lseek(fd, BLOCK_OFFSET(inode->i_block[i]), SEEK_SET);
		read(fd, buffer, block_size);
		for (int i = 0; i < 1024; i++)
		{
			printf("%c", buffer[i]);
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

void leArquivoPorNome(int fd, struct ext2_inode *inode, struct ext2_group_desc *group, char *nome, int *grupoAtual)
{
	printf("\n---Teste leitura de arquivo---\n");
	unsigned int index = 0;
	unsigned int valorInodeTmp = 0;
	char *nomeArquivo = nome;
	struct ext2_group_desc *grupoTemp = malloc(sizeof(struct ext2_group_desc));
	struct ext2_inode *inodeTemp = malloc(sizeof(struct ext2_inode));
	memcpy(grupoTemp, group, sizeof(struct ext2_group_desc));
	memcpy(inodeTemp, inode, sizeof(struct ext2_inode));
	// grupoTemp = group;
	read_dir(fd, inodeTemp, grupoTemp, &valorInodeTmp, nomeArquivo);
	if (valorInodeTmp == -1)
	{
		printf("ARQUIVO NÃO ENCONTRADO");
		return;
	}

	trocaGrupo(fd, &valorInodeTmp, grupoTemp, grupoAtual);
	// printf("TROCOU DE GRUPO");
	index = valorInodeTmp % super.s_inodes_per_group;
	read_inode(fd, index, grupoTemp, inodeTemp);
	printaArquivo(fd, inodeTemp);
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

int main(void)
{
	struct ext2_group_desc group;
	struct ext2_inode inode;
	int grupoAtual = 0;
	int fd;
	int i;
	char *buffer;

	/* open floppy device */

	if ((fd = open(FD_DEVICE, O_RDONLY)) < 0)
	{
		perror(FD_DEVICE);
		exit(1); /* error while opening the floppy device */
	}

	/* read super-block */

	lseek(fd, BASE_OFFSET, SEEK_SET);
	read(fd, &super, sizeof(super));
	// close(fd);

	if (super.s_magic != EXT2_SUPER_MAGIC)
	{
		fprintf(stderr, "Not a Ext2 filesystem\n");
		exit(1);
	}

	block_size = 1024 << super.s_log_block_size;
	printf("\n---LEITURA DO SUPERBLOCO---\n");
	/*printf("Reading super-block from device " FD_DEVICE ":\n"
		   "Volume name            : %s\n"
		   "Tamanho da imagem      : %u\n"
		   "Inodes count            : %u\n"
		   "Blocks count            : %u\n"
		   "Reserved blocks count   : %u\n"
		   "Free blocks count       : %u\n"
		   "Free inodes count       : %u\n"
		   "First data block        : %u\n"
		   "Block size              : %u\n"
		   "Blocks per group        : %u\n"
		   "Inodes per group        : %u\n"
		   "Creator OS              : %u\n"
		   "First non-reserved inode: %u\n"
		   "Size of inode structure : %hu\n"
		   "Magic number            : %hu\n",
		   super.s_volume_name,
		   (super.s_blocks_count * block_size),
		   super.s_inodes_count,
		   super.s_blocks_count,
		   super.s_r_blocks_count, // reserved blocks count
		   super.s_free_blocks_count,
		   super.s_free_inodes_count,
		   super.s_first_data_block,
		   block_size,
		   super.s_blocks_per_group,
		   super.s_inodes_per_group,
		   super.s_creator_os,
		   super.s_first_ino, // first non-reserved inode
		   super.s_inode_size,
		   super.s_magic);*/

	printf("\n---LEITURA DO PRIMEIRO GRUPO---\n");
	lseek(fd, BASE_OFFSET + block_size, SEEK_SET);
	read(fd, &group, sizeof(group));
	// close(fd);

	printf("\n\nReading first group-descriptor from device " FD_DEVICE ":\n"
		   "Blocks bitmap block: %u\n"
		   "Inodes bitmap block: %u\n"
		   "Inodes table block : %u\n"
		   "Free blocks count  : %u\n"
		   "Free inodes count  : %u\n"
		   "Directories count  : %u\n",
		   group.bg_block_bitmap,
		   group.bg_inode_bitmap,
		   group.bg_inode_table,
		   group.bg_free_blocks_count,
		   group.bg_free_inodes_count,
		   group.bg_used_dirs_count); /* directories count */

	printf("\n---LEITURA DO DIRETORIO ROOT---\n");
	/* read group descriptor */

	lseek(fd, BASE_OFFSET + block_size, SEEK_SET);
	read(fd, &group, sizeof(group));

	/* read root inode */

	read_inode(fd, 2, &group, &inode);

	printf("\n\nReading root inode\n"
		   "File mode: %hu\n"
		   "Owner UID: %hu\n"
		   "Size     : %u bytes\n"
		   "Blocks   : %u\n",
		   inode.i_mode,
		   inode.i_uid,
		   inode.i_size,
		   inode.i_blocks);

	for (i = 0; i < EXT2_N_BLOCKS; i++)
		if (i < EXT2_NDIR_BLOCKS) /* direct blocks */
			printf("Block %2u : %u\n", i, inode.i_block[i]);
		else if (i == EXT2_IND_BLOCK) /* single indirect block */
			printf("Single   : %u\n", inode.i_block[i]);
		else if (i == EXT2_DIND_BLOCK) /* double indirect block */
			printf("Double   : %u\n", inode.i_block[i]);
		else if (i == EXT2_TIND_BLOCK) /* triple indirect block */
			printf("Triple   : %u\n", inode.i_block[i]);

	unsigned int valor = 0;
	int index = 0;
	// Codigo para leitura do arquivo
	/*printf("\n---Teste leitura de arquivo---\n");
	unsigned int valor = 0;
	char* nomeArquivo = "hello.txt";
	read_dir(fd, &inode, &group, &valor, nomeArquivo);
	printf("INODE: %d\n", valor);

	read_inode(fd, valor, &group, &inode);

	char* teste = malloc(sizeof(char)*block_size);
	lseek(fd, BLOCK_OFFSET(inode.i_block[0]), SEEK_SET);
	read(fd, teste, block_size);
	for (int i = 0; i < inode.i_size; i++)
	{
	printf("%c", teste[i]);
	}
	printf("\n---%d---\n", BLOCK_OFFSET(inode.i_block[0]));*/

	// leArquivoPorNome(fd, &inode, &group, "hello.txt", &grupoAtual, super.s_inodes_per_group);
	//  unsigned int valor;
	//   leArquivoPorNome(fd, &inode, &group, "hello.txt");
	//   printaArquivo(fd, &inode, buffer);
	//  printf("grupo atual: %d\n", grupoAtual);
	//  read_dir(fd, &inode, &group, &valor, "/imagens2");
	//  trocaGrupo(fd, &valor, &group, super.s_inodes_per_group, &grupoAtual);
	//  printf("%ld\n", sizeof(struct ext2_group_desc));
	//  printf("%d\n", valor);
	//  printf("grupo atual: %d\n", grupoAtual);

	/*
	read_dir(fd, &inode, &group, &valor, "/livros");
	printf("Inode:%u", valor);
	int block_group = (valor - 1) / super.s_inodes_per_group;
	trocaGrupo(fd, &valor, &group, &grupoAtual);

	unsigned int index = (valor) % super.s_inodes_per_group;

	//read_inode(fd, index, &group, &inode);

	printf("-------------------\n");

	read_dir(fd, &inode, &group, &valor, "/religiosos");
	printf("Inode:%u", valor);
	block_group = (valor - 1) / super.s_inodes_per_group;
	trocaGrupo(fd, &valor, &group, &grupoAtual);

	index = (valor) % super.s_inodes_per_group;

	read_inode(fd, index, &group, &inode);

	read_dir(fd, &inode, &group, &valor, "");
*/ leArquivoPorNome(fd, &inode, &group, "hello.txt", &grupoAtual);
	funct_cd(fd, &inode, &group, &grupoAtual, "/livros");
	funct_cd(fd, &inode, &group, &grupoAtual, "/religiosos");
	printf("TENTATIVA DE LER O LIVRO");

	leArquivoPorNome(fd, &inode, &group, "/Biblia.txt", &grupoAtual);

	// printf("%d\n", block_group);
	// printf("\n---LEITURA DO PRIMEIRO GRUPO---\n");
	// lseek(fd, BASE_OFFSET + block_size + sizeof(struct ext2_group_desc) * block_group, SEEK_SET);
	// read(fd, &group, sizeof(group));
	printGroup(&group);
	// printInode(&inode);

	/*FUNCIONANDO
	unsigned int index = (valor - 1) % super.s_inodes_per_group;
	lseek(fd, BLOCK_OFFSET(group.bg_inode_table) + (index) * sizeof(struct ext2_inode), SEEK_SET);
	read(fd, &inode, sizeof(struct ext2_inode));
	*/

	// FUNCIONANDO
	// index = (valor) % super.s_inodes_per_group;
	// read_inode(fd, index, &group, &inode);
	// read_dir(fd, &inode, &group, &valor, "");

	// unsigned int containing_block = (index * super.s_inode_size) / block_size;

	// lseek(fd, BASE_OFFSET + block_size, SEEK_SET);
	// read(fd, &group, sizeof(group));

	/* read root inode */

	read_dir(fd, &inode, &group, &valor, "/documentos");
	// printf("TAMANHO: %d", inode.i_size);
	/*char *teste;
	read(fd, teste, block_size);
	// printf("FD: %d", fd);

	for (int i = 0; i < inode.i_size; i++)
	{
		printf("%c", teste[i]);
	}*/
	// printf("\n---%d---\n", BLOCK_OFFSET(inode.i_block[0]));

	exit(0);
} /* main() */
