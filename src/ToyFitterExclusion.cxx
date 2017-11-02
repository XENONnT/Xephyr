#include "ToyFitterExclusion.h"


ToyFitterExclusion::ToyFitterExclusion(TString fileName):errorHandler("ToyExclusion"){


    likeHood = NULL;
    dirPath  = fileName;
    OutDir = "./";
    treeName = "";
    likelihood_uncond = 0.;
    likelihood_cond = 0.;
    limit_converged = false;
    testStat_limit  = 0.;
    name_params.clear();
    DataNameHolder = "";
    mu_fit = 0.;
    testStat =0.;
    numberOfParams = 0.;
    randomizeMeasure = true;
    graph_of_quantiles = NULL;
    mu_limit = 0.;
    limit    = 0.;
}

void ToyFitterExclusion::for_each_tree(TFile *f, double (ToyFitterExclusion::*p2method)(double), TTree *outTree,  double mu, int stopAt){
    
    // TODO FIXME: this won't work for combination.
    // PROPOSAL: make the loop go through and match the number first, then 
    // match the name, so that you know the name and how many tree to load from combinedLikelihood,
    // and they are matchd by number. This way we can just change this function and the rest should 
    // run out of the box.

    // setting up branches on outTree
    mu_fit    = mu;
    testStat = 0.;
    numberOfParams =  likeHood->getParameters()->size();
    outTree->Branch("mu_fit", &mu_fit, "mu_fit/D");
    outTree->Branch("q_mu", &testStat, "testStat/D");
    outTree->Branch("n_params", &numberOfParams, "n_params/I");
    outTree->Branch("LL_cond", &likelihood_cond, "LL_cond/D");
    outTree->Branch("LL_uncond", &likelihood_uncond, "LL_uncond/D");
    outTree->Branch("true_params", true_params,"true_params[n_params]/D");
    outTree->Branch("measured_params", measured_params,"measured_params[n_params]/D");
    outTree->Branch("uncond_params", uncond_params,"uncond_params[n_params]/D");
    outTree->Branch("cond_params", cond_params,"cond_params[n_params]/D");
    outTree->Branch("name_params", &name_params);

    // input tree
    TTree *readTree = NULL;

    // Data handler
    dataHandler data("toyDMData");
    data.setData(DM_DATA);

    // setting random seed for measured param generation
    // TRandom likes a integer seed, the multiplication just makes sure that even if mu is small you get a new seed 
    rambo.SetSeed(((ULong_t) mu * 1000.)); 

    // looping over all tree that are prefixed with nameTree
    TIter next(f->GetListOfKeys());
    TObject *treeKey = NULL;
    int treeNumber =0;
    if(stopAt < 0 ) stopAt = f->GetListOfKeys()->GetSize();
    while ((treeKey = next()) && (treeNumber <= stopAt)) {
        
        if(TString(treeKey->GetName()).Contains(treeName)){
            readTree = (TTree*)f->Get(treeKey->GetName());
            treeNumber++;
        }
        else { 
            Info("fit", TString::Format("skipping, %s",treeKey->GetName()));
            continue; 
        }

        Info("fit", TString::Format("Matched tree name, %s", treeKey->GetName()));

        // fill true values from generated tree
        fillTrueParams(readTree);

        // set toy data for fit
        data.setDataTree(readTree);
        likeHood->setDataHandler(&data);

        // reset the parameter to their nominal initial value (othrwise takes longer to fit)
        likeHood->resetParameters();

        // Dice the measured parameters (note that if a parameter is free the t-value exytacted here has no effect)
        // Note: this MUST be called after "fillTrueParams"
        if(randomizeMeasure) measureParameters();

        // Fancy coding isn't it? ;)  
        // This is functional using a pointer to a function of ToyFitterExclusion
        // so that we can run this same loop for different purposes
        testStat = (this->*p2method)(mu);
        
        outTree->Fill();

    }

    if(treeNumber == 0) Error("for_each_tree",TString::Format("tree name %s was not found in file",treeName.Data()));

}

void ToyFitterExclusion::fit(double mu, int stopAt){
    
    // check if the tree is defined
    if(treeName == "") Error("fit", "You must set a tree name");

    // open the file and loop over all trees with given name
    TFile *f = TFile::Open(dirPath);
    if(f == NULL) Error("fit", TString::Format("file %s does not exist",dirPath.Data()));
    
    TFile f_out(OutDir + "post_fit_" + treeName + "_mufit"+ TString::Format("%1.2f",mu)+ ".root","RECREATE");

    // output tree, here intentionally all out tree will have the same name so we can hadd
    TTree *outTree = new TTree("out_" + treeName, "output tree for a given mu, hadd me");

    // read each tree in input file "f" nad applies the computeTS function to it 
    for_each_tree(f, &ToyFitterExclusion::computeTS, outTree, mu, stopAt );
        
    f_out.cd();
    outTree->Write();
    f_out.Close();
    f->Close();

}



void ToyFitterExclusion::spitTheLimit(TGraphAsymmErrors *ninety_quantiles, int stopAt){
    
    // check if the tree is defined
    if(treeName == "") Error("fit", "You must set a tree name");

     // open the file and loop over all trees with given name
     TFile *f = TFile::Open(dirPath);
     if(f == NULL) Error("fit", TString::Format("file %s does not exist",dirPath.Data()));
     
     TFile f_out(OutDir + "limit_" + treeName + ".root","RECREATE");
    
     graph_of_quantiles = ninety_quantiles;

     // output tree, here intentionally all out tree will have the same name so we can hadd
     TTree *outTree = new TTree("limit_" + treeName, "tree containing limits");
    
     // attach a few additional branch to out tree
     mu_limit = 0.;
     limit    = 0.;
     outTree->Branch("mu_limit", &mu_limit, "mu_limit/D");
     outTree->Branch("limit", &limit, "limit/D");
     outTree->Branch("testStat_limit", &testStat_limit, "testStat_limit/D");
     outTree->Branch("limit_converged", &limit_converged, "limit_converged/O");

     // read each tree in input file "f" nad applies the computeTS function to it 
     for_each_tree(f, &ToyFitterExclusion::limitLoop, outTree, -9., stopAt );
              
     f_out.cd();
     outTree->Write();
     f_out.Close();
     f->Close();
    
}

double ToyFitterExclusion::limitLoop(double initial_mu){

    // some checks
    if(graph_of_quantiles == NULL) Error("limitLoop", "graph of quantiles is not defined.");

    limit_converged = false;
    testStat_limit  = 0.;

    // from ROOT docs:  https://root.cern.ch/root/html/ROOT__Math__BrentMinimizer1D.html
    //                  https://root.cern.ch/numerical-minimization
    //                  https://root.cern.ch/how-implement-mathematical-function-inside-framework
    ROOT::Math::Functor1D func(this,&ToyFitterExclusion::eval_testStatMinuit); 
    
    ROOT::Math::BrentMinimizer1D bm; // this guy does numerical minimization scanning a range, not the fastest but quite robust.
    bm.SetFunction(func, 1. , 4.);   // interval from [0,4] in mu (by construction limit should be around 2.3)
    bm.SetNpx(3);                    // divide in 3 intervals
    limit_converged = bm.Minimize(10, 0.05, 0.01);     // max 10 iteration absolute error on mu = 0.05 relative error 1% 

    double q_stat  =  bm.FValMinimum();  // test stat value at limit

    mu_limit = bm.XMinimum();
    limit    = mu_limit * likeHood->getSignalMultiplier() * likeHood->getSignalDefaultNorm();

    Info("limitLoop", TString::Format("%s with TS val= %1.3f", (limit_converged ? "CONVERGED" : "NOT - CONVERGED" ), q_stat ) );
    Info("limitLoop",TString::Format("computed for %s ---> mu_limit= %f (~events) xsec = %E cm^2", likeHood->data->Name.Data(), mu_limit, limit));
    
    return bm.FValMinimum();
}


double ToyFitterExclusion::eval_testStatMinuit( double mu )  {

    // computing the difference between H0 qstat for a given mu and H_mu qstat.
    double qstat = computeTS(mu);
    double delta  = graph_of_quantiles->Eval(mu) - qstat;
    
    Debug("limitLoop", TString::Format("current_mu = %f   ;  current_qstat = %f  ;  delta = %f" ,mu, qstat, delta));        
    
    testStat_limit  =  qstat; // assuming the last call will set the qstat_limit properly
    
    return fabs(delta);
}


double ToyFitterExclusion::computeTS(double mu) {
    
    // we use q_tilde from equation 16 of: https://arxiv.org/abs/1007.1727
    // this is suitable for upper limit in which the parameter of interest must be >0.
    
    double qstat = 0.;
    
    // this is for limit case, where we are running many fit with different mu_test
    // on the same data tree. In those cases mu_hat is always the same, you don't want
    // to do unconditional fit again.
    bool DoMaximize  = ( DataNameHolder != likeHood->data->Name );
    Debug("computeTS", TString::Format("maximize %s --> %s", likeHood->data->Name.Data(), DataNameHolder.Data()));
    
    if(DoMaximize) DataNameHolder = likeHood->data->Name;

    // performing unconditional fit and saving it in unconditional likelihood  for outTree
    if(DoMaximize)    likelihood_uncond = likeHood->maximize(false) ;
    double mu_hat  = likeHood->getSigmaHat();

    double LL_denominator = likelihood_uncond;

    // printing 
    if(getPrintLevel() < WARNING) 
        likeHood->printCurrentParameters();
    
    // save parameters of unconditional fit 
    saveParameters(uncond_params);
    
    // override denominator in case mu_hat < 0.
    if( mu_hat < 0.){
        likeHood->POI->setCurrentValue(0.);    
        LL_denominator = likeHood->maximize(true) ;
    }

    // perform conditional fit
    likeHood->POI->setCurrentValue(mu);
    double LL_numerator = likeHood->maximize(true) ;
    
    // saving the conditional likelihood for outTree
    likelihood_cond = LL_numerator;
    
    // printing 
    if(getPrintLevel() < WARNING) 
        likeHood->printCurrentParameters();

    // save parameters of conditional fit 
    saveParameters(cond_params);

    qstat = 2. * ( LL_denominator - LL_numerator );  // -2*logLikelihoodRatio

    // following the q_tilde construction, this could have been put above with just return 0.
    // I just wanted to do the conditional fit anyway to return conditional parameter for study
    if( mu_hat > mu )  qstat = 0. ;

    

    return qstat;
}


 
void ToyFitterExclusion::saveParameters(double *outParams){

    map <int, LKParameter*> *params = likeHood->getParameters();
    
    int itr = 0;
    for(ParameterIterator ip=params->begin(); ip!=params->end(); ip++){
        
        LKParameter *param = ip->second;
        outParams[itr] = param->getCurrentValue();
        itr++;
    }    
}


void ToyFitterExclusion::saveNames(string *names){
    
        map <int, LKParameter*> *params = likeHood->getParameters();
        
        int itr = 0;
        for(ParameterIterator ip=params->begin(); ip!=params->end(); ip++){
            
            LKParameter *param = ip->second;
            names[itr] = param->getName();
            itr++;
        }    
}
    


void ToyFitterExclusion::fillTrueParams(TTree *inputTree){

    // retrive the previously saved TList of parameters (done in ToyGenerator)
    TIter iterateMe(inputTree->GetUserInfo());

    TParameter<double> *parameter = NULL;
    name_params.clear();

    int i = 0;
    // saving the parameters of the new tree
    while ((parameter = (TParameter<double>*)iterateMe())) {
        true_params[i] = parameter->GetVal();
        name_params.push_back(parameter->GetName());
        i++;
    }
}



void ToyFitterExclusion::measureParameters(){

    // randomize measure of parameters around the true value.
    
    map <int, LKParameter*> *params = likeHood->getParameters();
    
    Info("measureParameters", "Randomizing parameters:");

    int parItr = -1;
    for(ParameterIterator ip=params->begin(); ip!=params->end(); ip++){
            
        LKParameter *param = ip->second;
        parItr++;   // start from zero 
        
        if(param->getType() == FIXED_PARAMETER || param->isOfInterest() 
            || param->getType() == FREE_PARAMETER ) {

             measured_params[parItr] = param->getCurrentValue();
             Info("---->","Skipping paramater: " + param->getName()); 
             continue; 
        }
            
        // getting range
        double min = param->getMinimum();
        double max = param->getMaximum();
            
        // sample gauss tvalue for parameter
        double random_tvalue = 0.;
        random_tvalue = rambo.Gaus(true_params[parItr],1.);
        // extract again if out of range
        while(random_tvalue > max || random_tvalue < min)
                    random_tvalue = rambo.Gaus(true_params[parItr],1.);
    
    
        param->setT0value(random_tvalue);
        param->setCurrentValue(random_tvalue);  // you wanna start the fit from here
        measured_params[parItr] = random_tvalue;
        Info("---->",TString::Format("new T0-Value: %s = %1.2f",param->getName().Data(),random_tvalue)); 
    }

}


TGraphAsymmErrors ToyFitterExclusion::computeTSDistros(TString fileName, double *mu_list, int mu_size){


    TFile *f = new TFile(fileName);
    
    TTree *tree = (TTree*) f->Get(treeName);
 
    return computeTSDistros(tree, mu_list, mu_size );
    
        
}


TGraphAsymmErrors ToyFitterExclusion::computeTSDistros(TTree *tree, double *mu_list, int mu_size){

    TH1F *temp_h;
    TGraphAsymmErrors *q_mu_90 = new TGraphAsymmErrors(mu_size);

    TFile *out = new TFile("limit_distros.root", "RECREATE");

    const int    nq = 4;
    double xq[nq]= {0.5, 0.88, 0.90, 0.92};  // 2% unc
    double yq[nq] = {0};

    cout << "Quantiles: "<< endl;
    cout << "  mu"<< "\t  " << "50%" << "\t  " << "88%" << "\t  " << "90%" << "\t  " << "92%" << endl;
    for(int i =0; i < mu_size ; i++){

        TString histo_name = "q_mu_"+TString::Format("%1.2f",mu_list[i]);
        temp_h = new TH1F(histo_name, "", 1000, 0,10);

        tree->Draw("q_mu >>"+histo_name, "q_mu>= -0.01 && mu_fit =="+TString::Format("%1.2f",mu_list[i]));
        temp_h->GetQuantiles(nq, yq, xq);
        cout << TString::Format("%1.3f \t %1.3f \t %1.3f \t %1.3f \t %1.3f", mu_list[i], yq[0], yq[1], yq[2], yq[3] ) << endl;
        q_mu_90->SetPoint(i, mu_list[i], yq[2]);
        q_mu_90->SetPointError(i, 0.,0., yq[2] - yq[1], yq[3] - yq[2]);
        out->cd();
    
        temp_h->Write();
    

    }


    out->cd();
    q_mu_90->Write("quantiles");

    out->Close();

    return *q_mu_90;
}

