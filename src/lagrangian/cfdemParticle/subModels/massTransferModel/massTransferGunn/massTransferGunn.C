/*---------------------------------------------------------------------------*\
License

    This is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This code is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with this code.  If not, see <http://www.gnu.org/licenses/>.

    Copyright (C) 2015- Thomas Lichtenegger, JKU Linz, Austria

\*---------------------------------------------------------------------------*/

#include "error.H"
#include "massTransferGunn.H"
#include "addToRunTimeSelectionTable.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

defineTypeNameAndDebug(massTransferGunn, 0);

addToRunTimeSelectionTable(massTransferModel, massTransferGunn, dictionary);

// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

// Construct from components
massTransferGunn::massTransferGunn
(
    const dictionary& dict,
    cfdemCloudEnergy& sm
)
:
    massTransferModel(dict,sm),
    propsDict_(dict.subDict(typeName + "Props")),
    multiTypes_(false),
    expSherwood_(propsDict_.lookupOrDefault<bool>("expSherwood",false)),
    interpolation_(propsDict_.lookupOrDefault<bool>("interpolation",false)),
    verbose_(propsDict_.lookupOrDefault<bool>("verbose",false)),
    implicit_(propsDict_.lookupOrDefault<bool>("implicit",true)),
    SPartFluidName_(propsDict_.lookupOrDefault<word>("SPartFluidName","SPartFluid")),
    SPartFluid_
    (   IOobject
        (
            SPartFluidName_,
            sm.mesh().time().timeName(),
            sm.mesh(),
            IOobject::READ_IF_PRESENT,
            IOobject::AUTO_WRITE
        ),
        sm.mesh(),
        dimensionedScalar("zero", dimensionSet(1,-3,-1,0,0,0,0), 0.0)
    ),
    SPartFluidCoeffName_(propsDict_.lookupOrDefault<word>("SPartFluidCoeffName","SPartFluidCoeff")),
    SPartFluidCoeff_
    (   IOobject
        (
            SPartFluidCoeffName_,
            sm.mesh().time().timeName(),
            sm.mesh(),
            IOobject::READ_IF_PRESENT,
            IOobject::AUTO_WRITE
        ),
        sm.mesh(),
        dimensionedScalar("zero", dimensionSet(0,0,-1,0,0,0,0), 0.0)
    ),
    ReField_
    (   IOobject
        (
            "ReField",
            sm.mesh().time().timeName(),
            sm.mesh(),
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        sm.mesh(),
        dimensionedScalar("zero", dimensionSet(0,0,0,0,0,0,0), 0.0)
    ),
    ShField_
    (   IOobject
        (
            "ShField",
            sm.mesh().time().timeName(),
            sm.mesh(),
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        sm.mesh(),
        dimensionedScalar("zero", dimensionSet(0,0,0,0,0,0,0), 0.0)
    ),
    concFieldName_(propsDict_.lookupOrDefault<word>("concFieldName","C")),
    concField_(sm.mesh().lookupObject<volScalarField> (concFieldName_)),
    satConcFieldName_(propsDict_.lookupOrDefault<word>("satConcFieldName","Cs")),
    satConcField_(sm.mesh().lookupObject<volScalarField> (satConcFieldName_)),
    voidfractionFieldName_(propsDict_.lookupOrDefault<word>("voidfractionFieldName","voidfraction")),
    voidfraction_(sm.mesh().lookupObject<volScalarField> (voidfractionFieldName_)),
    maxSource_(1e30),
    velFieldName_(propsDict_.lookupOrDefault<word>("velFieldName","U")),
    U_(sm.mesh().lookupObject<volVectorField> (velFieldName_)),
    densityFieldName_(propsDict_.lookupOrDefault<word>("densityFieldName","rho")),
    rho_(sm.mesh().lookupObject<volScalarField> (densityFieldName_)),
    coupleDEM_(propsDict_.lookupOrDefault<bool>("coupleDEM",false)),
    partMassFluxName_(propsDict_.lookupOrDefault<word>("partMassFluxName","convectiveMassFlux")),
    partMassFluxCoeffRegName_(typeName + "partMassFluxCoeff"),
    partReRegName_(typeName + "partRe"),
    partShRegName_(typeName + "partSh"),
    scaleDia_(1.),
    typeCG_(propsDict_.lookupOrDefault<scalarList>("coarseGrainingFactors",scalarList(1,1.0)))
{
    particleCloud_.registerParticleProperty<double**>(partMassFluxName_,1);
    if(implicit_)
    {
        particleCloud_.registerParticleProperty<double**>(partMassFluxCoeffRegName_,1);
    }
    if(verbose_)
    {
        particleCloud_.registerParticleProperty<double**>(partReRegName_,1);
        particleCloud_.registerParticleProperty<double**>(partShRegName_,1);
    }


    if (propsDict_.found("maxSource"))
    {
        maxSource_=readScalar(propsDict_.lookup ("maxSource"));
        Info << "limiting eulerian source field to: " << maxSource_ << endl;
    }

    if (!implicit_)
    {
        SPartFluidCoeff_.writeOpt() = IOobject::NO_WRITE;
    }

    if (verbose_)
    {
        ReField_.writeOpt() = IOobject::AUTO_WRITE;
        ShField_.writeOpt() = IOobject::AUTO_WRITE;
        ReField_.write();
        ShField_.write();
        if (expSherwood_)
        {
          FatalError <<"Cannot read and create ShField at the same time!\n" << abort(FatalError);
        }
    }

    if (propsDict_.found("scale") && typeCG_.size()==1)
    {
        // if "scale" is specified and there's only one single type, use "scale"
        scaleDia_=scalar(readScalar(propsDict_.lookup("scale")));
        typeCG_[0] = scaleDia_;
    }
    else if (typeCG_.size()>1)
    {
        multiTypes_ = true;
    }
}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

massTransferGunn::~massTransferGunn()
{
}

// * * * * * * * * * * * * * * * private Member Functions  * * * * * * * * * * * * * //

// * * * * * * * * * * * * * * * * Member Fct  * * * * * * * * * * * * * * * //

void massTransferGunn::calcMassContribution()
{
    double**& partMassFlux_ = particleCloud_.getParticlePropertyRef<double**>(partMassFluxName_);

    // reset Scalar field
    SPartFluid_.primitiveFieldRef() = 0.0;

    #ifdef compre
       const volScalarField mufField = particleCloud_.turbulence().mu();
    #else
       const volScalarField mufField = particleCloud_.turbulence().nu()*rho_;
    #endif

    const volScalarField& D0Field_ = D0Field();

    if (typeCG_.size()>1 || typeCG_[0] > 1)
    {
        Info << "massTransferGunn using scale = " << typeCG_ << endl;
    }
    else if (particleCloud_.cg() > 1)
    {
        scaleDia_=particleCloud_.cg();
        Info << "massTransferGunn using scale from liggghts cg = " << scaleDia_ << endl;
    }

    // calc La based heat flux
    scalar voidfraction(1);
    vector Ufluid(0,0,0);
    scalar Cfluid(0);
    scalar Csfluid(0);
    label cellI=0;
    vector Us(0,0,0);
    scalar ds(0);
    scalar ds_scaled(0);
    scalar scaleDia3 = scaleDia_*scaleDia_*scaleDia_;
    scalar muf(0);
    scalar rhof(0);
    scalar magUr(0);
    scalar Rep(0);
    scalar Sc(0);
    scalar Shp(0);

    scalar cg = scaleDia_;
    label partType = 1;

    interpolationCellPoint<scalar> voidfractionInterpolator_(voidfraction_);
    interpolationCellPoint<vector> UInterpolator_(U_);
    interpolationCellPoint<scalar> CInterpolator_(concField_);

    for(int index = 0;index < particleCloud_.numberOfParticles(); ++index)
    {
            cellI = particleCloud_.cellIDs()[index][0];
            if(cellI >= 0)
            {
                if(interpolation_)
                {
                    vector position = particleCloud_.position(index);
                    voidfraction = voidfractionInterpolator_.interpolate(position,cellI);
                    Ufluid = UInterpolator_.interpolate(position,cellI);
                    Cfluid = CInterpolator_.interpolate(position,cellI);
                }
                else
                {
                    voidfraction = voidfraction_[cellI];
                    Ufluid = U_[cellI];
                    Cfluid = concField_[cellI];
                }

                if (voidfraction < 0.01)
                    voidfraction = 0.01;

                if (multiTypes_)
                {
                    partType = particleCloud_.particleType(index);
                    cg = typeCG_[partType - 1];
                    scaleDia3 = cg*cg*cg;
                }

                // calc relative velocity
                Us = particleCloud_.velocity(index);
                magUr = mag(Ufluid - Us);
                ds = 2.*particleCloud_.radius(index);
                ds_scaled = ds/cg;
                muf = mufField[cellI];
                rhof = rho_[cellI];
                Csfluid = satConcField_[cellI];

                Rep = ds_scaled * magUr * voidfraction * rhof/ muf;

                scalar D0 = D0Field_[cellI];

                Sc = max(SMALL, muf / (rhof*D0));

                Shp = Sherwood(voidfraction, Rep, Sc);

                scalar h = D0 * Shp / ds_scaled;
                scalar As = ds_scaled * ds_scaled * M_PI; // surface area of sphere

                // calc convective heat flux [W]
                massFlux(index, h, As, Cfluid, Csfluid, scaleDia3);

                if(verbose_)
                {
                    double**& partRe_ = particleCloud_.getParticlePropertyRef<double**>(partReRegName_);
                    double**& partSh_ = particleCloud_.getParticlePropertyRef<double**>(partShRegName_);
                    partRe_[index][0] = Rep;
                    partSh_[index][0] = Shp;
                }

                if(verbose_ && index >=0 && index <2)
                {
                    Pout << "partMassFlux = " << partMassFlux_[index][0] << endl;
                    Pout << "magUr = " << magUr << endl;
                    Pout << "D0 = " << D0 << endl;
                    Pout << "rho = " << rhof << endl;
                    Pout << "h = " << h << endl;
                    Pout << "ds = " << ds << endl;
                    Pout << "ds_scaled = " << ds_scaled << endl;
                    Pout << "As = " << As << endl;
                    Pout << "muf = " << muf << endl;
                    Pout << "Rep = " << Rep << endl;
                    Pout << "Sc = " << Sc << endl;
                    Pout << "Shp = " << Shp << endl;
                    Pout << "voidfraction = " << voidfraction << endl;
                    Pout << "Cfluid = " << Cfluid << endl;
                    Pout << "Csfluid = " << Csfluid << endl;
                }
            }
    }

    particleCloud_.averagingM().setScalarSum
    (
        SPartFluid_,
        partMassFlux_,
        particleCloud_.particleWeights(),
        NULL
    );

    SPartFluid_.primitiveFieldRef() /= -SPartFluid_.mesh().V();

    if(implicit_)
    {
        double**& partMassFluxCoeff_ = particleCloud_.getParticlePropertyRef<double**>(partMassFluxCoeffRegName_);
        SPartFluidCoeff_.primitiveFieldRef() = 0.0;

        particleCloud_.averagingM().setScalarSum
        (
            SPartFluidCoeff_,
            partMassFluxCoeff_,
            particleCloud_.particleWeights(),
            NULL
        );

        SPartFluidCoeff_.primitiveFieldRef() /= -SPartFluidCoeff_.mesh().V();
    }

    if(verbose_)
    {
        double**& partRe_ = particleCloud_.getParticlePropertyRef<double**>(partReRegName_);
        double**& partSh_ = particleCloud_.getParticlePropertyRef<double**>(partShRegName_);
        ReField_.primitiveFieldRef() = 0.0;
        ShField_.primitiveFieldRef() = 0.0;
        particleCloud_.averagingM().resetWeightFields();
        particleCloud_.averagingM().setScalarAverage
        (
            ReField_,
            partRe_,
            particleCloud_.particleWeights(),
            particleCloud_.averagingM().UsWeightField(),
            NULL
        );
        particleCloud_.averagingM().resetWeightFields();
        particleCloud_.averagingM().setScalarAverage
        (
            ShField_,
            partSh_,
            particleCloud_.particleWeights(),
            particleCloud_.averagingM().UsWeightField(),
            NULL
        );
    }

    // limit source term in explicit treatment
    if(!implicit_)
    {
        forAll(SPartFluid_,cellI)
        {
            scalar EuFieldInCell = SPartFluid_[cellI];

            if(mag(EuFieldInCell) > maxSource_ )
            {
                 Pout << "limiting source term\n"  << endl  ;
                 SPartFluid_[cellI] = sign(EuFieldInCell) * maxSource_;
            }
        }
    }

    SPartFluid_.correctBoundaryConditions();

    volScalarField minParticleWeights = particleCloud_.averagingM().UsWeightField();
    Info << "Minimum Particle Weight " << gMin(minParticleWeights) << endl;
    Info << "Minimum Fluid Concentration: " << gMin(concField_) << endl;
    Info << "Maximum Fluid Concentration: " << gMax(concField_) << endl;
}

void massTransferGunn::addMassContribution(volScalarField& Sm) const
{
    Sm += SPartFluid_;
}

void massTransferGunn::addMassCoefficient(volScalarField& Smi) const
{
    if(implicit_)
    {
        Smi += SPartFluidCoeff_;
    }
}

scalar massTransferGunn::Sherwood(scalar voidfraction, scalar Rep, scalar Sc) const
{
    return (7 - 10 * voidfraction + 5 * voidfraction * voidfraction) *
                        (1 + 0.7 * Foam::pow(Rep,0.2) * Foam::pow(Sc,0.33)) +
                        (1.33 - 2.4 * voidfraction + 1.2 * voidfraction * voidfraction) *
                        Foam::pow(Rep,0.7) * Foam::pow(Sc,0.33);
}

void massTransferGunn::massFlux(label index, scalar h, scalar As, scalar Cfluid, scalar Csfluid, scalar cg3)
{
    scalar hAs = h * As * cg3;
    double**& partMassFlux_ = particleCloud_.getParticlePropertyRef<double**>(partMassFluxName_);

    if (particleCloud_.getParticleEffVolFactors())
    {
        scalar effVolFac = particleCloud_.particleEffVolFactor(index);
        hAs *= effVolFac;
    }

    partMassFlux_[index][0] = - hAs * Csfluid;
    if(!implicit_)
    {
        partMassFlux_[index][0] += hAs * Cfluid;
    }
    else
    {
        double**& partMassFluxCoeff_ = particleCloud_.getParticlePropertyRef<double**>(partMassFluxCoeffRegName_);
        partMassFluxCoeff_[index][0] = hAs;
    }
}

void massTransferGunn::giveData()
{
    Info << "total convective particle-fluid mass flux [kg/s] (Eulerian) = " << gSum(SPartFluid_*1.0*SPartFluid_.mesh().V()) << endl;

    double**& partMassFlux_ = particleCloud_.getParticlePropertyRef<double**>(partMassFluxName_);
    particleCloud_.dataExchangeM().giveData(partMassFluxName_,"scalar-atom", partMassFlux_);
}

void massTransferGunn::postFlow()
{
    // send mass flux to DEM
    if (coupleDEM_)
    {
        if(implicit_)
        {
            label cellI;
            scalar Cfluid(0.0);
            interpolationCellPoint<scalar> CInterpolator_(concField_);
            double**& partMassFlux_ = particleCloud_.getParticlePropertyRef<double**>(partMassFluxName_);
            double**& partMassFluxCoeff_ = particleCloud_.getParticlePropertyRef<double**>(partMassFluxCoeffRegName_);

            for(int index = 0;index < particleCloud_.numberOfParticles(); ++index)
            {
                cellI = particleCloud_.cellIDs()[index][0];
                if(cellI >= 0)
                {
                    if(interpolation_)
                    {
                        vector position = particleCloud_.position(index);
                        Cfluid = CInterpolator_.interpolate(position,cellI);
                    }
                    else
                    {
                        Cfluid = concField_[cellI];
                    }

                    partMassFlux_[index][0] += Cfluid * partMassFluxCoeff_[index][0];

                    if(verbose_ && index >=0 && index <2)
                    {
                        Pout << "partMassFlux = " << partMassFlux_[index][0] << endl;
                        Pout << "Cfluid = " << Cfluid << endl;
                    }
                }
            }
        }

        giveData();
    }
}

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam

// ************************************************************************* //

