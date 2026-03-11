#include <mpi.h>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

using namespace std;

struct Clause{
    int lit1;
    int lit2;
    int lit3;
};

struct SolverStats{
    long long nodesVisited = 0;
    long long clauseChecks = 0;
};

struct ExperimentConfig{
    int numVars = 22;
    int numClauses = 90;
    int repetitions = 5;
    int baseSeed = 12345;
    int splitVariable = 1;
    int fixedFormula = 0;
    int forceUnsat = 0;
    int simulateMs = 0;
    int csvEnabled = 1;
    string csvPrefix = "resultados_sat3";
};

struct TaskPayload{
    int repetition;
    int splitValue;
};

struct WorkerReport{
    int workerRank;
    int repetition;
    int splitValue;
    int sat;
    double computeSec;
    long long nodesVisited;
    long long clauseChecks;
    long long maxRssKB;
};

const int TAG_TASK = 100;
const int TAG_RESULT = 200;

void imprimirUso(const char* progName)
{
    cout << "Uso: " << progName << " [opciones]\n"
         << "  --vars N         Numero de variables (default: 22)\n"
         << "  --clauses M      Numero de clausulas 3-SAT (default: 90)\n"
         << "  --reps R         Repeticiones para medicion (default: 5)\n"
         << "  --seed S         Semilla base aleatoria (default: 12345)\n"
         << "  --split-var V    Variable usada para dividir trabajo (default: 1)\n"
         << "  --fixed          Usa formula fija pequena (la del ejemplo)\n"
         << "  --force-unsat    Fuerza instancia UNSAT con clausulas contradictorias\n"
         << "  --simulate-ms T  Retardo artificial por worker en ms (default: 0)\n"
         << "  --csv-prefix P   Prefijo para CSV (default: resultados_sat3)\n"
         << "  --no-csv         Desactiva escritura automatica de CSV\n"
         << "  --help           Muestra esta ayuda\n";
}

bool parsearArgumentos(int argc, char** argv, ExperimentConfig& cfg,
                      bool& showHelp, string& error){
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--vars") == 0) {
            if (i + 1 >= argc) {
                error = "Falta valor para --vars";
                return false;
            }
            cfg.numVars = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--clauses") == 0) {
            if (i + 1 >= argc) {
                error = "Falta valor para --clauses";
                return false;
            }
            cfg.numClauses = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--reps") == 0) {
            if (i + 1 >= argc) {
                error = "Falta valor para --reps";
                return false;
            }
            cfg.repetitions = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--seed") == 0) {
            if (i + 1 >= argc) {
                error = "Falta valor para --seed";
                return false;
            }
            cfg.baseSeed = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--split-var") == 0) {
            if (i + 1 >= argc) {
                error = "Falta valor para --split-var";
                return false;
            }
            cfg.splitVariable = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--simulate-ms") == 0) {
            if (i + 1 >= argc) {
                error = "Falta valor para --simulate-ms";
                return false;
            }
            cfg.simulateMs = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--csv-prefix") == 0) {
            if (i + 1 >= argc) {
                error = "Falta valor para --csv-prefix";
                return false;
            }
            cfg.csvPrefix = argv[++i];
        }
        else if (strcmp(argv[i], "--fixed") == 0) {
            cfg.fixedFormula = 1;
        }
        else if (strcmp(argv[i], "--force-unsat") == 0) {
            cfg.forceUnsat = 1;
        }
        else if (strcmp(argv[i], "--no-csv") == 0) {
            cfg.csvEnabled = 0;
        }
        else if (strcmp(argv[i], "--help") == 0) {
            showHelp = true;
            return true;
        }
        else {
            error = string("Argumento no reconocido: ") + argv[i];
            return false;
        }
    }

    if (cfg.numVars < 2) {
        error = "--vars debe ser >= 2";
        return false;
    }

    if (cfg.numClauses < 1) {
        error = "--clauses debe ser >= 1";
        return false;
    }

    if (cfg.repetitions < 1) {
        error = "--reps debe ser >= 1";
        return false;
    }

    if (cfg.splitVariable < 1 || cfg.splitVariable > cfg.numVars) {
        error = "--split-var debe estar en [1, numVars]";
        return false;
    }

    if (cfg.simulateMs < 0) {
        error = "--simulate-ms no puede ser negativo";
        return false;
    }

    if (cfg.fixedFormula == 1 && cfg.numVars < 5) {
        error = "Con --fixed necesitas --vars >= 5";
        return false;
    }

    if (cfg.csvEnabled == 1 && cfg.csvPrefix.empty()) {
        error = "--csv-prefix no puede estar vacio";
        return false;
    }

    return true;
}

bool archivoVacio(const string& ruta){
    ifstream in(ruta, ios::binary | ios::ate);
    if (!in.is_open()) {
        return true;
    }

    return in.tellg() == 0;
}

string generarRunId(){
    auto now = chrono::system_clock::now();
    time_t nowTime = chrono::system_clock::to_time_t(now);
    tm localTm{};

#if defined(_WIN32)
    localtime_s(&localTm, &nowTime);
#else
    localtime_r(&nowTime, &localTm);
#endif

    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &localTm);
    return string(buffer);
}

long long obtenerMaxRssKB()
{
#if defined(__linux__) || defined(__APPLE__)
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return static_cast<long long>(usage.ru_maxrss);
    }
#endif
    return -1;
}

vector<Clause> crearFormulaFija3SAT(){
    vector<Clause> formula;
    formula.push_back({1, 2, 3});
    formula.push_back({-1, 2, 4});
    formula.push_back({1, -2, 5});
    formula.push_back({-3, -4, 5});
    formula.push_back({-1, -5, 2});
    formula.push_back({3, 4, -5});
    return formula;
}

vector<Clause> crearFormulaAleatoria3SAT(int numVars, int numClauses, uint32_t seed){
    vector<Clause> formula;
    formula.reserve(static_cast<size_t>(numClauses));

    mt19937 rng(seed);
    uniform_int_distribution<int> varDist(1, numVars);
    uniform_int_distribution<int> signDist(0, 1);

    for (int i = 0; i < numClauses; i++) {
        int v1 = varDist(rng);
        int v2 = varDist(rng);
        int v3 = varDist(rng);

        if (numVars >= 3) {
            while (v2 == v1) {
                v2 = varDist(rng);
            }

            while (v3 == v1 || v3 == v2) {
                v3 = varDist(rng);
            }
        }

        int l1 = signDist(rng) ? v1 : -v1;
        int l2 = signDist(rng) ? v2 : -v2;
        int l3 = signDist(rng) ? v3 : -v3;

        formula.push_back({l1, l2, l3});
    }

    return formula;
}

void forzarUnsat(vector<Clause>& formula, int splitVariable){
    formula.push_back({splitVariable, splitVariable, splitVariable});
    formula.push_back({-splitVariable, -splitVariable, -splitVariable});
}

int valorLiteral(int lit, const vector<int>& asignacion){
    int var = abs(lit);
    int valor = asignacion[var];

    if (valor == -1) {
        return -1;
    }

    if (lit > 0) {
        return valor;
    }

    return 1 - valor;
}

int estadoClausula(const Clause& c, const vector<int>& asignacion,
                  SolverStats& stats){
    stats.clauseChecks++;

    int valores[3] = {
        valorLiteral(c.lit1, asignacion),
        valorLiteral(c.lit2, asignacion),
        valorLiteral(c.lit3, asignacion)
    };

    bool hayNoAsignado = false;

    for (int i = 0; i < 3; i++) {
        if (valores[i] == 1) {
            return 1;
        }

        if (valores[i] == -1) {
            hayNoAsignado = true;
        }
    }

    if (hayNoAsignado) {
        return 0;
    }

    return -1;
}

bool hayConflicto(const vector<Clause>& formula, const vector<int>& asignacion,
                 SolverStats& stats)
{
    for (const Clause& c : formula) {
        if (estadoClausula(c, asignacion, stats) == -1) {
            return true;
        }
    }

    return false;
}

bool formulaSatisfecha(const vector<Clause>& formula, const vector<int>& asignacion,
                      SolverStats& stats)
{
    for (const Clause& c : formula) {
        if (estadoClausula(c, asignacion, stats) != 1) {
            return false;
        }
    }

    return true;
}

int elegirVariable(const vector<int>& asignacion, int numVars)
{
    for (int v = 1; v <= numVars; v++) {
        if (asignacion[v] == -1) {
            return v;
        }
    }

    return -1;
}

bool resolverDPLL(const vector<Clause>& formula, vector<int>& asignacion,
                 int numVars, SolverStats& stats)
{
    stats.nodesVisited++;

    if (hayConflicto(formula, asignacion, stats)) {
        return false;
    }

    if (formulaSatisfecha(formula, asignacion, stats)) {
        return true;
    }

    int var = elegirVariable(asignacion, numVars);

    if (var == -1) {
        return false;
    }

    asignacion[var] = 0;
    if (resolverDPLL(formula, asignacion, numVars, stats)) {
        return true;
    }

    asignacion[var] = 1;
    if (resolverDPLL(formula, asignacion, numVars, stats)) {
        return true;
    }

    asignacion[var] = -1;
    return false;
}

WorkerReport ejecutar3SATWorker(const ExperimentConfig& cfg,
                                const TaskPayload& task,
                                int rank)
{
    WorkerReport report;
    report.workerRank = rank;
    report.repetition = task.repetition;
    report.splitValue = task.splitValue;
    report.sat = 0;
    report.computeSec = 0.0;
    report.nodesVisited = 0;
    report.clauseChecks = 0;
    report.maxRssKB = -1;

    vector<Clause> formula;
    if (cfg.fixedFormula == 1) {
        formula = crearFormulaFija3SAT();
    }
    else {
        uint32_t repSeed = static_cast<uint32_t>(cfg.baseSeed + task.repetition);
        formula = crearFormulaAleatoria3SAT(cfg.numVars, cfg.numClauses, repSeed);
    }

    if (cfg.forceUnsat == 1) {
        forzarUnsat(formula, cfg.splitVariable);
    }

    vector<int> asignacion(static_cast<size_t>(cfg.numVars + 1), -1);
    asignacion[cfg.splitVariable] = task.splitValue;

    SolverStats stats;

    double t0 = MPI_Wtime();
    bool sat = resolverDPLL(formula, asignacion, cfg.numVars, stats);

    if (cfg.simulateMs > 0) {
        this_thread::sleep_for(chrono::milliseconds(cfg.simulateMs));
    }

    double t1 = MPI_Wtime();

    report.sat = sat ? 1 : 0;
    report.computeSec = t1 - t0;
    report.nodesVisited = stats.nodesVisited;
    report.clauseChecks = stats.clauseChecks;
    report.maxRssKB = obtenerMaxRssKB();
    return report;
}

void ejecutarManager(const ExperimentConfig& cfg, int size)
{
    string runId = generarRunId();
    bool csvActivo = false;
    string csvDetalleRuta;
    string csvResumenRuta;
    ofstream csvDetalle;
    ofstream csvResumen;

    if (cfg.csvEnabled == 1) {
        csvDetalleRuta = cfg.csvPrefix + "_detalle.csv";
        csvResumenRuta = cfg.csvPrefix + "_resumen.csv";

        bool detalleVacio = archivoVacio(csvDetalleRuta);
        bool resumenVacio = archivoVacio(csvResumenRuta);

        csvDetalle.open(csvDetalleRuta, ios::app);
        csvResumen.open(csvResumenRuta, ios::app);

        if (!csvDetalle.is_open() || !csvResumen.is_open()) {
            cout << "Advertencia: no se pudieron abrir CSV para escritura. "
                 << "Se continuara sin guardar archivos.\n";
        }
        else {
            csvActivo = true;

            if (detalleVacio) {
                csvDetalle << "run_id,vars,clauses,reps,seed,split_var,fixed,force_unsat,simulate_ms,"
                          << "repetition,worker_rank,split_value,worker_sat,worker_compute_sec,"
                          << "nodes_visited,clause_checks,max_rss_kb,rep_global_sat\n";
            }

            if (resumenVacio) {
                csvResumen << "run_id,vars,clauses,reps,seed,split_var,fixed,force_unsat,simulate_ms,"
                          << "total_wall_sec,avg_rep_sec,avg_worker_compute_sec,max_worker_compute_sec,"
                          << "sat_repetitions,total_nodes,total_clause_checks,max_rss_kb,total_messages,total_bytes\n";
            }

            cout << "CSV detalle: " << csvDetalleRuta << "\n";
            cout << "CSV resumen: " << csvResumenRuta << "\n";
            cout << "Run ID: " << runId << "\n";
        }
    }

    cout << "Manager iniciado\n";
    cout << "Config: vars=" << cfg.numVars
         << ", clauses=" << cfg.numClauses
         << ", reps=" << cfg.repetitions
         << ", seed=" << cfg.baseSeed
         << ", splitVar=" << cfg.splitVariable
         << ", fixed=" << cfg.fixedFormula
         << ", forceUnsat=" << cfg.forceUnsat
         << ", simulateMs=" << cfg.simulateMs
         << ", csv=" << (cfg.csvEnabled ? "on" : "off") << "\n";

    double managerStart = MPI_Wtime();

    int satRepetitions = 0;
    double sumWorkerCompute = 0.0;
    double maxWorkerCompute = 0.0;
    long long sumNodes = 0;
    long long sumClauseChecks = 0;
    long long maxRssSeen = -1;

    int numWorkers = size - 1;

    for (int rep = 0; rep < cfg.repetitions; rep++) {
        for (int w = 1; w < size; w++) {
            int splitValue = (w - 1) % numWorkers;
            TaskPayload tarea = {rep, splitValue};
            MPI_Send(&tarea, static_cast<int>(sizeof(TaskPayload)), MPI_BYTE,
                     w, TAG_TASK, MPI_COMM_WORLD);
        }

        int satGlobal = 0;
        vector<WorkerReport> reportesRep;
        reportesRep.reserve(numWorkers);

        for (int i = 0; i < numWorkers; i++) {
            WorkerReport report;
            MPI_Status status;

            MPI_Recv(&report, static_cast<int>(sizeof(WorkerReport)), MPI_BYTE,
                     MPI_ANY_SOURCE, TAG_RESULT, MPI_COMM_WORLD, &status);

            satGlobal = satGlobal || report.sat;
            reportesRep.push_back(report);
            sumWorkerCompute += report.computeSec;
            if (report.computeSec > maxWorkerCompute) {
                maxWorkerCompute = report.computeSec;
            }

            sumNodes += report.nodesVisited;
            sumClauseChecks += report.clauseChecks;
            if (report.maxRssKB > maxRssSeen) {
                maxRssSeen = report.maxRssKB;
            }

            cout << "Rep " << rep
                 << " | Worker" << report.workerRank
                 << " | x" << cfg.splitVariable << "=" << report.splitValue
                 << " | " << (report.sat ? "SAT" : "UNSAT")
                 << " | t=" << report.computeSec << " s"
                 << " | nodos=" << report.nodesVisited
                 << " | checks=" << report.clauseChecks
                 << " | maxRSS=" << report.maxRssKB << " KB\n";
        }

        if (csvActivo) {
            for (const WorkerReport& report : reportesRep) {
                csvDetalle << runId << ","
                          << cfg.numVars << ","
                          << cfg.numClauses << ","
                          << cfg.repetitions << ","
                          << cfg.baseSeed << ","
                          << cfg.splitVariable << ","
                          << cfg.fixedFormula << ","
                          << cfg.forceUnsat << ","
                          << cfg.simulateMs << ","
                          << rep << ","
                          << report.workerRank << ","
                          << report.splitValue << ","
                          << report.sat << ","
                          << report.computeSec << ","
                          << report.nodesVisited << ","
                          << report.clauseChecks << ","
                          << report.maxRssKB << ","
                          << satGlobal << "\n";
            }
        }

        if (satGlobal) {
            satRepetitions++;
        }

        cout << "Rep " << rep << " -> Resultado global 3-SAT: "
             << (satGlobal ? "SAT" : "UNSAT") << "\n";
    }

    TaskPayload fin = {-1, -1};
    for (int w = 1; w < size; w++) {
        MPI_Send(&fin, static_cast<int>(sizeof(TaskPayload)), MPI_BYTE,
                 w, TAG_TASK, MPI_COMM_WORLD);
    }

    double managerEnd = MPI_Wtime();
    double totalWall = managerEnd - managerStart;

    long long totalMessages = static_cast<long long>(cfg.repetitions) * 2 * numWorkers + numWorkers;
    long long bytesTask = static_cast<long long>(cfg.repetitions) * numWorkers * sizeof(TaskPayload)
                       + numWorkers * sizeof(TaskPayload);
    long long bytesResult = static_cast<long long>(cfg.repetitions) * numWorkers * sizeof(WorkerReport);
    long long totalBytes = bytesTask + bytesResult;

    cout << "\n===== RESUMEN MEDICION =====\n";
    cout << "Tiempo total manager: " << totalWall << " s\n";
    cout << "Promedio por repeticion: " << (totalWall / cfg.repetitions) << " s\n";
    cout << "Tiempo promedio de compute por worker: "
         << (sumWorkerCompute / (cfg.repetitions * 2.0)) << " s\n";
    cout << "Tiempo maximo de compute en un worker: " << maxWorkerCompute << " s\n";
    cout << "Repeticiones SAT: " << satRepetitions << "/" << cfg.repetitions << "\n";
    cout << "Nodos DPLL totales: " << sumNodes << "\n";
    cout << "Chequeos de clausulas totales: " << sumClauseChecks << "\n";
    cout << "Max RSS observado: " << maxRssSeen << " KB\n";
    cout << "Mensajes MPI (aprox): " << totalMessages << "\n";
    cout << "Trafico MPI util (aprox): " << totalBytes << " bytes\n";
    cout << "============================\n";

    if (csvActivo) {
        csvResumen << runId << ","
                  << cfg.numVars << ","
                  << cfg.numClauses << ","
                  << cfg.repetitions << ","
                  << cfg.baseSeed << ","
                  << cfg.splitVariable << ","
                  << cfg.fixedFormula << ","
                  << cfg.forceUnsat << ","
                  << cfg.simulateMs << ","
                  << totalWall << ","
                  << (totalWall / cfg.repetitions) << ","
                  << (sumWorkerCompute / (cfg.repetitions * 2.0)) << ","
                  << maxWorkerCompute << ","
                  << satRepetitions << ","
                  << sumNodes << ","
                  << sumClauseChecks << ","
                  << maxRssSeen << ","
                  << totalMessages << ","
                  << totalBytes << "\n";

        csvDetalle.flush();
        csvResumen.flush();
    }
}

void ejecutarWorker(int rank, const ExperimentConfig& cfg){
    while (true){
        TaskPayload task;
        MPI_Recv(&task, static_cast<int>(sizeof(TaskPayload)), MPI_BYTE,
                 0, TAG_TASK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        if (task.repetition < 0) {
            break;
        }

        WorkerReport report = ejecutar3SATWorker(cfg, task, rank);
        MPI_Send(&report, static_cast<int>(sizeof(WorkerReport)), MPI_BYTE,
                 0, TAG_RESULT, MPI_COMM_WORLD);
    }
}

int main(int argc, char** argv){
    ExperimentConfig cfg;
    bool showHelp = false;
    string error;

    if (!parsearArgumentos(argc, argv, cfg, showHelp, error)){
        cerr << "Error: " << error << "\n";
        imprimirUso(argv[0]);
        return 1;
    }

    if (showHelp){
        imprimirUso(argv[0]);
        return 0;
    }

    MPI_Init(&argc, &argv);

    int rank = 0;
    int size = 0;

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (size < 2){
        if (rank == 0) {
            cout << "Este ejemplo requiere minimo 2 procesos: "
                 << "manager(0) + 1 o mas workers.\n";
        }

        MPI_Finalize();
        return 1;
    }

    if (rank == 0){
        ejecutarManager(cfg, size);
    }
    else{
        ejecutarWorker(rank, cfg);
    }

    MPI_Finalize();
    return 0;
}
