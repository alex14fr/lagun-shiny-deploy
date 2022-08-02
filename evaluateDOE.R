#
# This file is subject to the terms and conditions defined in
# the file 'LICENSE', which is part of this source code.
#

# module evaluateDOE
get.listDOE <- function(Xopt, lb, ub, ntestMC, callback) {
  nb.inputs <- ncol(Xopt)
  nb.samples <- nrow(Xopt)
  ntotDOE <- ntestMC + 3
  Xinit <- (Xopt - repmat(lb,nb.samples,1))/repmat(ub-lb,nb.samples,1)
  minit <- mindist(Xinit)
  dinit <- discrepancyL2centered(Xinit)
  linit <- dchi2LHS(Xinit,nb.samples,0,1)
  pinit <- maxprocrit(Xinit)
  Xhalton <- halton(nb.samples,nb.inputs)
  mhalton <- mindist(Xhalton)
  dhalton <- discrepancyL2centered(Xhalton)
  lhalton <- dchi2LHS(Xhalton,nb.samples,0,1)
  phalton <- maxprocrit(Xhalton)
  callback(1)
  Xsobol <- sobol(nb.samples,nb.inputs)
  msobol <- mindist(Xsobol)
  dsobol <-  discrepancyL2centered(Xsobol)
  lsobol <- dchi2LHS(Xsobol,nb.samples,0,1)
  psobol <- maxprocrit(Xsobol)
  callback(2)
  if (nb.samples > floor(1.4*nb.inputs)) {
    Xfaure <- runif.faure(nb.samples,nb.inputs)$design
    mfaure <- mindist(Xfaure)
    dfaure <-  discrepancyL2centered(Xfaure)
    lfaure <- dchi2LHS(Xfaure,nb.samples,0,1)
    pfaure <- maxprocrit(Xfaure)
  }else{
    mfaure <- NA
    dfaure <- NA
    lfaure <- NA
    pfaure <- NA
  }
  callback(3)
  mMC <- matrix(0,nrow=ntestMC,ncol=1)
  dMC <- matrix(0,nrow=ntestMC,ncol=1)
  pMC <- matrix(0,nrow=ntestMC,ncol=1)
  lMC <- matrix(0,nrow=ntestMC,ncol=nb.inputs)
  for (i in 1:ntestMC){
    XMC <- matrix(runif(nb.samples*nb.inputs),ncol=nb.inputs)
    mMC[i] <- mindist(XMC)
    dMC[i] <- discrepancyL2centered(XMC)
    pMC[i] <- maxprocrit(XMC)
    lMC[i,] <- dchi2LHS(XMC,nb.samples,0,1)
    callback(i)
  }
  
  list(
    nb.samples = nb.samples, nb.inputs = nb.inputs,
    minit=minit,dinit=dinit,linit=linit,pinit=pinit,
    mhalton=mhalton,dhalton=dhalton,lhalton=lhalton,phalton=phalton,
    msobol=msobol,dsobol=dsobol,lsobol=lsobol,psobol=psobol,
    mfaure=mfaure,dfaure=dfaure,lfaure=lfaure,pfaure=pfaure,
    mMC=mMC,dMC=dMC,pMC=pMC,lMC=lMC,ntestMC=ntestMC
  )
}

compute.checkDOE <- function(Xopt, Xinfos, ntestMC, all.numeric){
  
  removeModal()
  listDOE <- NULL
  if (all.numeric){
    Xbounds <- get.bounds(Xinfos)
    ntotDOE <- ntestMC + 3
    callback <- function(i) {
      incProgress(1/ntotDOE, detail = paste("DOE ", 3 + i,"/",ntotDOE)) 
    }
    withProgress(message = 'Generating Multiple Concurrent DOEs', value = 0, {
      listDOE <- get.listDOE(Xopt, Xbounds[1,,drop=F], Xbounds[2,,drop=F], ntestMC, callback)
    })
  }else{
    showModal(modalDialog(HTML(
      "No evaluation available for DOE with categorical variables.")
      , title = "Warning !")
    )
  }
  return(listDOE)
  
}

plot.checkDOE <- function(listDOE) {
  visudata <- data.frame(doetype='Faure',maximin=listDOE$mfaure,discrepancy=listDOE$dfaure,maxpro=listDOE$pfaure)
  visudata <- rbind(visudata,data.frame(doetype='Halton',maximin=listDOE$mhalton,discrepancy=listDOE$dhalton,maxpro=listDOE$phalton))
  visudata <- rbind(visudata,data.frame(doetype='Sobol',maximin=listDOE$msobol,discrepancy=listDOE$dsobol,maxpro=listDOE$psobol))
  visudata <- rbind(visudata,data.frame(doetype=rep('MC',listDOE$ntestMC),maximin=listDOE$mMC,discrepancy=listDOE$dMC,maxpro=listDOE$pMC))
  visudata <- rbind(visudata,data.frame(doetype='DOE',maximin=listDOE$minit,discrepancy=listDOE$dinit,maxpro=listDOE$pinit))
  layout(
    subplot(
      plot_ly(visudata, x = ~doetype, y = ~maximin, type = "box"),
      plot_ly(visudata, x = ~doetype, y = ~discrepancy, type = "box"),
      plot_ly(visudata, x = ~doetype, y = ~maxpro, type = "box"),
      margin = 0.05
    ),
    xaxis = list(title="DOE type"),
    yaxis=list(title="Maximin Criterion"),
    xaxis2 = list(title="DOE type"),
    yaxis2=list(title="Discrepancy Criterion"),
    xaxis3 = list(title="DOE type"),
    yaxis3=list(title="MaxProj Criterion"),
    showlegend = FALSE,
    title = "DOE Comparison"
  )
}

plot.checkDOE.LHS <- function(listDOE) {
  nb.inputs <- listDOE$nb.inputs
  visudata <- data.frame(doetype='LHS',Chi2 = matrix(rep(0,nb.inputs),ncol=nb.inputs))
  if (!is.na(listDOE$lfaure)){
    visudata <- rbind(visudata,data.frame(doetype='Faure',Chi2 = listDOE$lfaure))
  }
  visudata <- rbind(visudata,data.frame(doetype='Halton',Chi2 = listDOE$lhalton))
  visudata <- rbind(visudata,data.frame(doetype='Sobol',Chi2 = listDOE$lsobol))
  visudata <- rbind(visudata,data.frame(doetype='MC',Chi2 = listDOE$lMC))
  visudata <- rbind(visudata,data.frame(doetype='DOE',Chi2 = listDOE$linit))
  colnames(visudata) <- c("doetype",paste0("X",1:nb.inputs))
  vv <- melt(visudata,id="doetype")
  layout(
    plot_ly(vv, x = ~value, y = ~doetype, color = ~as.factor(variable), type = "box", orientation = "h"),
    title="Chi2 Distance to LHS", boxmode = "group", 
    xaxis=list(title="Chi2 Distance"), yaxis=list(title="DOE type")
  )
}

evaluateDOE.ui <- function(id) {
  ns <- NS(id)
  tagList(
    fluidRow(actionButton(ns("check"), "Launch Check",icon = icon("bar-chart"), class = "btn-info",width='100%')),
    br(),
    fluidRow(
      plotlyOutput(ns("plot")),
      hr(),
      plotlyOutput(ns("plotLHS"))
    )
  )
}

evaluateDOE.server <- function(input, output, session, DOE, settings) {
  
  ns <- session$ns
  
  checkDOE <- reactiveValues(listDOE = NULL)
  
  all.numeric <- reactive({get.nb.num(DOE$Xinfos) == length(DOE$Xinfos)})
  
  observeEvent(input$dismiss, {
    removeModal()
    checkDOE$listDOE <- NULL
  })
  
  observeEvent(input$check, {
    req(DOE$Xopt, DOE$Xinfos, length(DOE$Xinfos) >= 2)
    checkDOE$listDOE <- compute.checkDOE(DOE$Xopt, DOE$Xinfos, settings$ntestMC, all.numeric())
  })
  
  observeEvent(DOE$Xopt, {
    checkDOE$listDOE <- NULL
  })
  
  output$plot <- renderPlotly({
    req(DOE$Xopt, DOE$Xinfos, input$check, checkDOE$listDOE)
    plot.checkDOE(checkDOE$listDOE)
  })
  
  output$plotLHS <- renderPlotly({
    req(DOE$Xopt, DOE$Xinfos, input$check, checkDOE$listDOE)
    plot.checkDOE.LHS(checkDOE$listDOE)
  })
}
