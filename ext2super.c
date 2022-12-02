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

#include <sys/wait.h>
#include <sys/resource.h>
#include <readline/readline.h>
#include <readline/history.h>

#define BASE_OFFSET 1024 /* locates beginning of the super block (first group) */
#define FD_DEVICE "./myext2image.img"
#define EXT2_SUPER_MAGIC 0xEF53 /* the floppy disk device */
#define BLOCK_OFFSET(block) (BASE_OFFSET + (block - 1) * block_size)

static unsigned int block_size = 0; /* block size (to be calculated) */

static void read_inode(int fd, int inode_no, const struct ext2_group_desc *group, struct ext2_inode *inode)
{
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

void trocaGrupo(int fd, unsigned int *valor, struct ext2_group_desc *group, int inodePerGroup, int *grupoAtual)
{
	printf("\n--- TROCANDO O GRUPO ---\n");
	printf("INODE: %u", *valor);
	unsigned int block_group = ((*valor) - 1) / inodePerGroup;
	if (block_group != (*grupoAtual))
	{
		printf("trocou de grupo");
		*grupoAtual = block_group;
		printf("%d\n", block_group);
		lseek(fd, BASE_OFFSET + block_size + sizeof(struct ext2_group_desc) * block_group, SEEK_SET);
		read(fd, group, sizeof(group));
	}
}

void printaArquivo(int fd, const struct ext2_inode *inode)
{
	printf("\nTENTANDO LER O AQRUIVO AQUI\n");
	char *buffer = malloc(sizeof(char) * block_size);
	int tempSize = inode->i_size;
	//printf("\n---Print do arquivo---\n");
	lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
	read(fd, buffer, block_size);
	for (int i = 0; (tempSize) > 0; i++)
	{
		printf("%c", buffer[i]);
		tempSize = tempSize - sizeof(char);
	}
	free(buffer);
}

void leArquivoPorNome(int fd, struct ext2_inode *inode, struct ext2_group_desc *group, char *nome, int *grupoAtual, int inodesPorGrupo)
{
	printf("\n---Teste leitura de arquivo---\n");
	unsigned int valorInodeTmp = 0;
	char *nomeArquivo = nome;
	struct ext2_group_desc *grupoTemp = malloc(sizeof(struct ext2_group_desc));
	struct ext2_inode *inodeTemp = malloc(sizeof(struct ext2_inode));
	memcpy(grupoTemp,group,sizeof(struct ext2_group_desc));
	memcpy(inodeTemp,inode,sizeof(struct ext2_inode));
	//grupoTemp = group;
	read_dir(fd, inodeTemp, grupoTemp, &valorInodeTmp, nomeArquivo);
	if (valorInodeTmp == -1)
	{
		printf("ARQUIVO NÃO ENCONTRADO");
		return;
	}

	trocaGrupo(fd, &valorInodeTmp, grupoTemp, inodesPorGrupo, grupoAtual);
	// printf("TROCOU DE GRUPO");
	read_inode(fd, valorInodeTmp, grupoTemp, inodeTemp);
	
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

void info(int fd, struct ext2_super_block *super)
{
	printf("Reading super-block from device " FD_DEVICE ":\n"
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

		   super->s_volume_name,
		   (super->s_blocks_count * block_size),
		   (super->s_free_blocks_count * block_size) / 1024,
		   // #BUG_CONHECIDO: é mostrado mais Free space do que o Campiolo mostra
		   super->s_free_inodes_count,
		   super->s_free_blocks_count,
		   block_size,
		   super->s_inode_size,
		   (super->s_blocks_count / super->s_blocks_per_group), // quantos / (quantos por grupo)
		   /* OBS acima: essa divisão pode retornar um a menos caso o ultimo grupo não tenha
		   exatamente todo o número de blocos certo, por causa de uma imagem não divisivel pelo tamanho.
		   #BUG_CONHECIDO: quando documentar bugs conhecidos, colocar esse.
		   */
		   super->s_blocks_per_group,
		   super->s_inodes_per_group,
		   (super->s_inodes_per_group / (block_size / sizeof(struct ext2_inode))));

	/*
	infos nao uteis pro comando
	super->s_inodes_count,
	super->s_blocks_count,
	super->s_r_blocks_count, //  reserved blocks count
	super->s_first_data_block,
	block_size,
	super->s_creator_os,
	super->s_first_ino, // first non-reserved inode
	super->s_magic);
	*/
}

int shell()
{
    char *entrada = malloc(100 * sizeof(char));
    char **argumentos = malloc(10 * sizeof(char *));
    char *token; //Cada parte do comando;
    int numArg = 0; // Número de partes do comando;

    while (1)
    {
        numArg = 0;

        entrada = readline("[nEXT2Shell]>>> ");
        if (!strcmp(entrada, ""))//Reinicia o processo de entrada se nenhum comando for digitado;
        {
            continue;
        }

        entrada[strcspn(entrada, "\n")] = 0;//Consome o '\n' que o readline coloca;
        add_history(entrada);//Acrescenta o comando no histórico;

        token = strtok(entrada, " ");
        if (!(strcasecmp(token, "exit")))//Sai quando for digitado exit;
        {
            return 0;
        }
        else if (!(strcasecmp(token, "cd")))//Faz funcionar o cd;
        {
            token = strtok(NULL, " ");
            chdir(token);//Muda o diretório;
            continue;
        }
//==============================================================
//Esse pedaço separa as partes do comando
        argumentos[numArg] = token;
        numArg++;
        while (token != NULL)
        {
            token = strtok(NULL, " ");

            argumentos[numArg] = token;
            numArg++;
        }
//==============================================================
        if (!(strcmp(argumentos[numArg - 2], "&")))//Verifica se foi digitado o '&';
        {
            argumentos[numArg - 2] = NULL;
            numArg--;
            pid_t pid = fork();
            if (pid == 0)
            {
                if (execvp(argumentos[0], argumentos) == -1)
                {
                    printf("\nErro: Comando nao suportado\n");
                    exit(1);
                }

                for (int i = 0; i < numArg; i++)//Limpa o vetor dos pedaços do comando
                {
                    free(argumentos[i]);
                    argumentos[i] = NULL;
                }
            }
            else
            {
                printf("\n");
                continue;
            }
        }

        pid_t pid = fork();

        if (pid == 0)
        {
            if (execvp(argumentos[0], argumentos) == -1)
            {
                printf("\nErro: Comando nao suportado\n");
                exit(1);
            }

            for (int i = 0; i < numArg; i++)
            {
                free(argumentos[i]);
                argumentos[i] = NULL;
            }
        }

        int estado;
        struct rusage ru;
        pid_t pid_filho = wait4(pid, &estado, 0, &ru);//Espera o filho executar;

        printf("\n");
    }

    return 0;
}

int main(void)
{
	printf("\nTeste\n");

	struct ext2_super_block super;
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

	// Exibe informações do disco e do sistema de arquivos
	info(fd, &super);

	printf("\n---LEITURA DO PRIMEIRO GRUPO---\n");
	lseek(fd, BASE_OFFSET + block_size, SEEK_SET);
	read(fd, &group, sizeof(group));
	// close(fd);

	printf("Reading first group-descriptor from device " FD_DEVICE ":\n"
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

	printf("Reading root inode\n"
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

	leArquivoPorNome(fd, &inode, &group, "hello.txt", &grupoAtual, super.s_inodes_per_group);
	// unsigned int valor;
	//  leArquivoPorNome(fd, &inode, &group, "hello.txt");
	//  printaArquivo(fd, &inode, buffer);
	// printf("grupo atual: %d\n", grupoAtual);
	// read_dir(fd, &inode, &group, &valor, "/imagens2");
	// trocaGrupo(fd, &valor, &group, super.s_inodes_per_group, &grupoAtual);
	// printf("%ld\n", sizeof(struct ext2_group_desc));
	// printf("%d\n", valor);
	// printf("grupo atual: %d\n", grupoAtual);

	read_dir(fd, &inode, &group, &valor, "imagens");
	printf("CHEGOU");
	int block_group = (valor - 1) / super.s_inodes_per_group;
	trocaGrupo(fd, &valor, &group, super.s_inodes_per_group, &grupoAtual);
	// printf("%d\n", block_group);
	// printf("\n---LEITURA DO PRIMEIRO GRUPO---\n");
	// lseek(fd, BASE_OFFSET + block_size + sizeof(struct ext2_group_desc) * block_group, SEEK_SET);
	// read(fd, &group, sizeof(group));

	printf("Reading first group-descriptor from device " FD_DEVICE ":\n"
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

	/*FUNCIONANDO
	unsigned int index = (valor - 1) % super.s_inodes_per_group;
	lseek(fd, BLOCK_OFFSET(group.bg_inode_table) + (index) * sizeof(struct ext2_inode), SEEK_SET);
	read(fd, &inode, sizeof(struct ext2_inode));
	*/

	// FUNCIONANDO
	unsigned int index = (valor) % super.s_inodes_per_group;
	read_inode(fd, index, &group, &inode);
	read_dir(fd, &inode, &group, &valor, "");

	// unsigned int containing_block = (index * super.s_inode_size) / block_size;

	// lseek(fd, BASE_OFFSET + block_size, SEEK_SET);
	// read(fd, &group, sizeof(group));

	/* read root inode */

	printf("Reading root inode\n"
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
