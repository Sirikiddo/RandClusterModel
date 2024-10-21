#include <iostream>

using namespace std;
static int fib(int n);

int main() {

	int a;

	cout << "До какого числа выводить:";
	cin >> a;
	cout << "0,";

	for (int i = 1; i <= a; i++) {

		if (i == a) {
			cout << fib(i);
		}

		else {
			cout << fib(i) << ",";
		}
	}

}

static int fib(int n)
{
	if (n <= 2) return 1;
	else return fib(n - 1) + fib(n - 2);
}