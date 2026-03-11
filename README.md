# SAT3_MPI

        mpiuser@emisor-VirtualBox:~/cloud$ mpirun -np 3 ./sat3 --help
        Uso: ./sat3 [opciones]
          --vars N         Numero de variables (default: 22)
          --clauses M      Numero de clausulas 3-SAT (default: 90)
          --reps R         Repeticiones para medicion (default: 5)
          --seed S         Semilla base aleatoria (default: 12345)
          --split-var V    Variable usada para dividir trabajo (default: 1)
          --fixed          Usa formula fija pequena (la del ejemplo)
          --force-unsat    Fuerza instancia UNSAT con clausulas contradictorias
          --simulate-ms T  Retardo artificial por worker en ms (default: 0)
          --csv-prefix P   Prefijo para CSV (default: resultados_sat3)
          --no-csv         Desactiva escritura automatica de CSV
          --help           Muestra esta ayuda
