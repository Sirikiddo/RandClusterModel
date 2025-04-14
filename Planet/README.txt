Скопировать папки bin, glm, include и lib
В свойствах проекта в C/C++ общие доп каталоги библиотек подключить:
D:\programming\cpp_projects\HexGrid\HexGrid\include
D:\programming\cpp_projects\HexGrid\HexGrid\glm\glm
Скопировать файлы glew32.dll, glfw3.dll, opengl32.dll, freetype.dll в папку, где хранится exe-файл(создается после нажатия запуска в VS Studio)

В компоновщике в доп зависимостях добавить:
D:\programming\cpp_projects\HexGrid\HexGrid\lib

В доп зависимостях биболиотек компоновщик/ввод добавить: 
glew32.lib
glfw3dll.lib
opengl32.lib
freetype.lib

В папку lib добавить freetype.lib

Как работать с гитом
текущий статус - git status
получить изменения с сервера git pull
Общий процесс состоит из трех шагов:
1) git add имя_файла.расширение
2) git commit -m "что вы сделали"
3) git push
Можно использовать для простоты добавления git add -i (вызывается интерактивное меню)
