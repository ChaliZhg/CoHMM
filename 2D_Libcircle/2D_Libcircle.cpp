#include <iostream>
#include <cstdlib>
#include <cstring>

#include <mpi.h>
#include <libcircle.h>


#include "CoHMM_DaD.hpp"

//Rely on globals for nTasks, step, and phase. At least for spawning purposes
unsigned int step;
unsigned int phase;
unsigned int nTasks;
bool doKriging;
bool doCoMD;
char redis_host[48];

//Methods to convert tasks to libcircle tasks (strings)
void buildTaskString(char * buffer, bool doKriging, bool doCoMD, unsigned int step, unsigned int phase, unsigned int tid, const char * redis_host)
{
	//Space delimited because that can't be in a hostname for obvious reasons
	sprintf(buffer, "%d %d %d %d %d  %s", doKriging, doCoMD, step, phase, tid, redis_host);
}

void unBuildTaskString(char * taskString, bool & doKriging, bool & doCoMD, unsigned int & step, unsigned int & phase, unsigned int & tid, char * redis_host)
{
	//Tokenize and convert the string
	//First is kriging
	char * token = std::strtok(taskString, " ");
	if(atoi(token) == 1)
	{
		doKriging = true;
	}
	else
	{
		doKriging = false;
	}
	//Next is comd
	token = std::strtok(nullptr, " ");
	if(atoi(token) == 1)
	{
		doCoMD = true;
	}
	else
	{
		doCoMD = false;
	}
	//Now step
	token = std::strtok(nullptr, " ");
	step = atoi(token);
	//Phase
	token = std::strtok(nullptr, " ");
	phase = atoi(token);
	//tid
	token = std::strtok(nullptr, " ");
	phase = atoi(token);
	//Host
	token = std::strtok(nullptr, " ");
	strcpy(redis_host, token);
}

void processTasks(CIRCLE_handle *handle)
{
	char taskString[96];
	//Dominic did reference thing and it worked
	handle->dequeue(&taskString[0]);
	bool krig;
	bool comd;
	unsigned int t;
	unsigned int p;
	unsigned int i;
	char redis[48];
	unBuildTaskString(taskString, krig, comd, t, p, i, redis);
	std::cout << "Test" << std::endl;
	cloudFlux(krig, comd, t, p, i, redis);
	//Is libcircle okay with not having a return? Becuase we don't
}

void buildTasks(CIRCLE_handle *handle)
{
	//If this doesn't happen on rank 0, it fails horribly
	///TODO: is it more efficient to stack allocate before or during loop?
	char taskString[96];
	std::cout << "nTasks: " << nTasks << std::endl;
	for(unsigned int i = 0; i < nTasks; i++)
	{
		buildTaskString(taskString, doKriging, doCoMD, step, phase, i, redis_host);
		handle->enqueue(taskString);
	}
}

void doLibcircleTasks()
{
	///TODO: Is there a way to not pay the cost of init and finalize on every single flux?
	//Init libcircle(?): Don't pass args as we do that stuff on our own
	int rank = CIRCLE_init(0, nullptr, CIRCLE_DEFAULT_FLAGS);
	//Run task creation method
	std::cout << "Trying to build?" << std::endl;
	CIRCLE_cb_create(&buildTasks);
	/*
	CIRCLE_handle * handle = CIRCLE_get_handle();
	char taskString[96];
	for(unsigned int i = 0; i < nTasks; i++)
	{
		buildTaskString(taskString, doKriging, doCoMD, step, phase, i, redis_host);
		//buildTaskString(taskString, false , true , 4, 5, 2, "localhost");
		handle->enqueue(taskString);
	}
	*/
	//Say what to do with tasks
	CIRCLE_cb_process(&processTasks);
	//Run tasks
	//CIRCLE_begin();
	//Finalize libcircle (?)
	CIRCLE_finalize();

}

int main(int argc, char ** argv)
{
	//<dim_x> <dim_y> <nsteps> <redis_server> <database error threshold> <Kriging error threshold> <Gaussian noise strength>
	//dimX dimY nSteps redis_server 
	if( argc != 5)
	{
		std::cerr <<  "./2D_DaDTest <dim_x> <dim_y> <nsteps> <redis_server>" << std::endl;
		return 1;
	}
	//Set up parameters
	doKriging = true;
	doCoMD = false;
	int dims[2] = {atoi(argv[1]), atoi(argv[2])};
	double dt[2] = {0.1, 0.1};
	double delta[2] = {1.0, 1.0};
	double gamma[3];
	gamma[0] = 0; //mom_gamma
	gamma[1] = gamma[0]; //strain_gamma
	gamma[2] = 0.1 * gamma[1];//en_gamma
	strcpy(redis_host, argv[4]);

	unsigned int numSteps = atoi(argv[3]);

	//Initialize
	std::cout << "Initializing " << dims[0] << " by " << dims[1] << " grid" << std::endl;
	initEverything(doKriging, doCoMD, dims, dt, delta, gamma);
	std::cout << "Initialized" << std::endl;
	//Loop
	std::cout << "Running for " << numSteps << " iterations" << std::endl;
	for(step = 0; step < numSteps; step++)
	{
		std::cout << step << ": Vising to Verifying" << std::endl;
		outputVTK(doKriging, doCoMD, dims, dt, delta, gamma, step, argv[4]);
		//Do a short circuit test
		if(tryShortCircuit(dims, step, argv[4]))
		{
			//Short circuit succeeded
			std::cout << step << ": Short Circuit Successful, on to the next step!" << std::endl;
		}
		else
		{
			std::cout << step << ": First Flux" << std::endl;
			nTasks = prepFirstFlux(doKriging, doCoMD, dims, dt, delta, gamma, step, argv[4]);
			std::cout << step << ": Doing " << nTasks << " fluxes" << std::endl;
			phase = 0;
			doLibcircleTasks();
			std::cout << step << ": Second Flux" << std::endl;
			nTasks = prepSecondFlux(doKriging, doCoMD, dims, dt, delta, gamma, step, argv[4]);
			std::cout << step << ": Doing " << nTasks << " fluxes" << std::endl;
			phase = 1;
			doLibcircleTasks();
			std::cout << step << ": Third Flux" << std::endl;
			nTasks = prepThirdFlux(doKriging, doCoMD, dims, dt, delta, gamma, step, argv[4]);
			std::cout << step << ": Doing " << nTasks << " fluxes" << std::endl;
			phase = 2;
			doLibcircleTasks();
			std::cout << step << ": Last Flux" << std::endl;
			nTasks = prepLastFlux(doKriging, doCoMD, dims, dt, delta, gamma, step, argv[4]);
			std::cout << step << ": Doing " << nTasks << " fluxes" << std::endl;
			phase = 3;
			doLibcircleTasks();
			std::cout << step << ": Finish Step, no Fluxes" << std::endl;
			finishStep(doKriging, doCoMD, dims, dt, delta, gamma, step, argv[4]);
		}
	}
	//Final vis
	std::cout << numSteps << ": Vising to Verifying" << std::endl;
	outputVTK(doKriging, doCoMD, dims, dt, delta, gamma, numSteps, argv[4]);
	std::cout << "Ran for " << numSteps << " iterations" << std::endl;
	return 0;
}

