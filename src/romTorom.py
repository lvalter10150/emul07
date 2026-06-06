#!/bin/python3

# -------------------------------------------------------------
# Fichier de transformation du fichier rom_xo7.bin en rom_x07.h
# -------------------------------------------------------------

def binary_to_hex(file_path):
    hex_values = []

    with open(file_path, 'rb') as file:
        byte = file.read(1)
        while byte:
            hex_values.append(format(byte[0], '02x'))
            byte = file.read(1)

    return hex_values

def generate_c_array(hex_values):
    c_array = "{\n    "
    for i, value in enumerate(hex_values):
        c_array += "0x" + value
        if i != len(hex_values) - 1:
            c_array += ", "
        if (i + 1) % 10 == 0:  # Ajoute un saut de ligne toutes les 10 valeurs pour une meilleure lisibilité
            c_array += "\n    "
    c_array += "\n};"
    return c_array

def main():
    file_path = "rom_xo7.bin"  # Remplacez "example.bin" par le chemin de votre fichier binaire
    hex_values = binary_to_hex(file_path)
    c_array = generate_c_array(hex_values)

    print(len(hex_values))
    
    #print("Tableau en hexadécimal pour utilisation en C:")
    #print(c_array)

if __name__ == "__main__":
    main()

