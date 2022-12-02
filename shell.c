#include <stdio.h>

int main()
{
	char *entrada = malloc(100 * sizeof(char));
	char **argumentos = malloc(10 * sizeof(char *));
	char *token;	// Cada parte do comando;
	int numArg = 0; // Número de partes do comando;

	while (1)
	{
		numArg = 0;

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
		else if (!(strcasecmp(token, "cd"))) // Faz funcionar o cd;
		{
			token = strtok(NULL, " ");
			chdir(token); // Muda o diretório;
			continue;
		}
		//==============================================================
		// Esse pedaço separa as partes do comando
		argumentos[numArg] = token;
		numArg++;
		while (token != NULL)
		{
			token = strtok(NULL, " ");

			argumentos[numArg] = token;
			numArg++;
		}
		//==============================================================
		if (!(strcmp(argumentos[numArg - 2], "&"))) // Verifica se foi digitado o '&';
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

				for (int i = 0; i < numArg; i++) // Limpa o vetor dos pedaços do comando
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
		pid_t pid_filho = wait4(pid, &estado, 0, &ru); // Espera o filho executar;

		printf("\n");
	}

	return 0;
}