/*---------------------------------------------------------------------------*\
    CFDEMcoupling - Open Source CFD-DEM coupling

    CFDEMcoupling is part of the CFDEMproject
    www.cfdem.com
                                Christoph Goniva, christoph.goniva@cfdem.com
                                Copyright (C) 1991-2009 OpenCFD Ltd.
                                Copyright (C) 2009-2012 JKU, Linz
                                Copyright (C) 2012-     DCS Computing GmbH,Linz
-------------------------------------------------------------------------------
License
    This file is part of CFDEMcoupling.

    CFDEMcoupling is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    CFDEMcoupling is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with CFDEMcoupling.  If not, see <http://www.gnu.org/licenses/>.

Application
    cfdemSolverPisoFreeStreaming

Description
    Transient solver for incompressible flow.
    Turbulence modelling is generic, i.e. laminar, RAS or LES may be selected.
    The code is an evolution of the solver pisoFoam in OpenFOAM(R) 1.6,
    where additional functionality for CFD-DEM coupling is added.
    the particles follow the fluid velocity
\*---------------------------------------------------------------------------*/

#include "fvCFD.H"
#include "singlePhaseTransportModel.H"
#include "turbulentTransportModel.H"
#include "pisoControl.H"
#include "fvOptions.H"

#include "cfdemCloudRec.H"

#include "cfdemCloud.H"
#include "implicitCouple.H"
#include "clockModel.H"
#include "smoothingModel.H"
#include "forceModel.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

int main(int argc, char *argv[])
{
    #include "postProcess.H"
    #include "setRootCase.H"
    #include "createTime.H"
    #include "createMesh.H"
    #include "createControl.H"
    #include "createFields.H"
    #include "createFvOptions.H"
    #include "initContinuityErrs.H"

    cfdemCloudRec<cfdemCloud> particleCloud(mesh);
    // * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //
    Info<< "\nStarting time loop\n" << endl;
    while (runTime.loop())
    {

        particleCloud.clockM().start(1,"Global");

        Info<< "Time = " << runTime.timeName() << nl << endl;

        #include "CourantNo.H"

        // do particle stuff
        particleCloud.clockM().start(2,"Coupling");

        particleCloud.evolve(voidfraction,Us,U);


        particleCloud.clockM().stop("Coupling");

        particleCloud.clockM().start(26,"Flow");

        if(particleCloud.solveFlow())
        {
            // Pressure-velocity PISO corrector
            {
                // Momentum predictor
                 #include "UEqn.H"

                // --- PISO loop

                while (piso.correct())
                {
                    #include "pEqn.H"
                }
            }

            laminarTransport.correct();
            turbulence->correct();
        }
        else
        {
            Info << "skipping flow solution." << endl;
        }

        runTime.write();

        Info<< "ExecutionTime = " << runTime.elapsedCpuTime() << " s"
            << "  ClockTime = " << runTime.elapsedClockTime() << " s"
            << nl << endl;

        particleCloud.clockM().stop("Flow");
        particleCloud.clockM().stop("Global");
    }

    Info<< "End\n" << endl;

    return 0;
}


// ************************************************************************* //
