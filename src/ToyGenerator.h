#ifndef TOY_GENERATOR
#define TOY_GENERATOR


#include "TRandom.h"
#include "TString.h"
#include "XeUtils.h"
#include "XeLikelihoods.h"
#include "XeStat.h"
#include <map>

/**
 * \class ToyGenerator 
 * \biref Helper class to generate toys given an input likelihood. 
 * It can generate toys for the calibration dataset and for the "science data" dataset.
*/
class ToyGenerator: public errorHandler {

    public:
        //! Constructor: @param sampleName: name of the file and tree prefix
        //! @param outDir: path to dir where you want to save toys
        ToyGenerator(TString sampleName, TString outDir);

        //! \brief pointer to the defined likelihood
        void setLikelihood(pdfLikelihood *like) {likeHood = like;};

        //! \brief the total number of events to which the calibtration pdfs will be rescaled.
        //
        //! The fraction of events given to each pdf component is defined in the likelihood. 
        //! This is just an overall scale.
        void setAverageCalibrationEvents(double evnt) {averageCalEvnt = evnt;} ;


        //! \brief the total number of events to which the science data pdfs will be rescaled.
        //
        //! This overrides the number of events set in the likelihood.
        //! The fraction of events given to each pdf component is defined in the likelihood. 
        //! This is just an overall scale. Typically you don't want to set this, but you must 
        //! set "setAverageCalibrationEvents" instead.
        void setAverageDataEvents(double evnt) {averageDataEvnt = evnt;} ;

        //! \brief generate N toys of the calibration dataset.
        void generateCalibration(int N);

        //! \brief generate N toys of the 'science data' dataset with injected signal fraction 'mu'.
        void generateData(double mu, int N);

        //! set the seed for the toy generation. YOU MUST CHANGE THIS for any run.
        void setSeed(int seed);

        //! \brief this will randomize the nominal value of the nuissance parameters, according to their distro.
        //
        //! The generated toys will not be generated now from nominal distro. YOU MUST change seed
        //! for each repetition of this.
        void randomizeNuissanceParameter();


    private:

        double    averageCalEvnt;
        double    averageDataEvnt;
        TRandom3  randomizeMyass;
        TString   treeName;
        TString   dir;
        pdfLikelihood *likeHood;
};




#endif