#include <iostream>
#include <stack>
using namespace std;

int main()
{
    stack<string> pilha;
    pilha.push("0");
    pilha.push("1");
    pilha.push("2");
    pilha.push("3");
    cout << pilha.top() << endl;
    pilha.pop();
    cout << pilha.top() << endl;
    for (int i = 0; i <= pilha.size() + 1; i++)
    {
        pilha.pop();
        if (pilha.empty())
        {
            cout << "Fiquei vazia na iteração: " << i + 1 << endl;
            break;
        }
    }

    return 0;
}
