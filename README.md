# SAT3_MPI


### El problema 3‑SAT pertenece a la clase NP.


Master / Workers

| Rank | Máquina | Rol     |
| ---- | ------- | ------- |
| 0    | emisor  | Manager |
| 1    | clon1   | Worker1  |
| 2    | clon2   | Worker2  |


Qué significa cada línea de logs:



        Rep 0 | Worker1 | x1=0 | UNSAT | t=0.300648 s | nodos=87801 | checks=8023633 | maxRSS=11176 KB

| Campo        | Significado                       |
| ------------ | --------------------------------- |
| Rep 0        | repetición del experimento        |
| Worker1      | nodo que ejecutó                  |
| x1=0         | subproblema asignado              |
| UNSAT        | no encontró solución              |
| t=0.300648 s | tiempo de cómputo                 |
| nodos        | nodos explorados en el árbol DPLL |
| checks       | evaluaciones de cláusulas         |
| maxRSS       | memoria máxima usada              |



---

ejemplo ejecucción:

                /usr/bin/time -v mpirun -np 3 --hostfile hosts.txt ./sat3 --vars 50 --clauses 250 --reps 20 --seed 7 --csv-prefix exp3 2>&1 | tee exp3.log

---

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


### Métricas: 

| archivo              | para qué sirve               |
| -------------------- | ---------------------------- |
| baseline.log         | salida completa del programa |
| baseline_detalle.csv | datos por worker             |
| baseline_resumen.csv | métricas agregadas           |


### Experiemento 1
        NUM_VARS = 22
        clausulas = 90
        np = 3

Espacio de búsqueda:

2^22 ≈ 4 millones


### Experiemento 2
        NUM_VARS = 40
        clausulas = 180
        np = 3

### Experiemento 3
        NUM_VARS = 50
        clausulas = 250
        np = 3

### Experiemento 3_1
        NUM_VARS = 50
        clausulas = 250
        np = 2 

### Experiemento 4 fallo de nodo
        NUM_VARS = 40
        clausulas = 180
        np = 3


