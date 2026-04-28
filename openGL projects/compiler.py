import subprocess

file_name = "app"
includes = "-Idependencies/include"
lib_paths = "-Ldependencies/lib"
lib = "-lfreeglut -lglu32 -lopengl32 -lgdi32"

def main():
    # compile
    ret = subprocess.call(f'g++ -c main.cpp {includes} -o main.o')
    if ret != 0:
        print("Compilation failed.")
        return
    # link
    ret = subprocess.call(f"g++ main.o -o {file_name} {lib_paths} {lib}")
    if ret != 0:
        print("Linking failed.")
        return
    print("Build successful! Run app.exe to play.")

if "__main__" == __name__:
    main()