#
# This file is subject to the terms and conditions defined in
# the file 'LICENSE', which is part of this source code.
#

if (as.numeric(R.version$major) == 3){
  if (as.numeric(R.version$minor) < 6.1) stop("R version > 3.6 is required.")
}

# nettoyage du worskspace
rm(list = ls())

source("loadPackages.R")
source("utilityFunctions.R")
source("modules/navigation.R", local = TRUE)

detectclosejs <- HTML("
function goodbye(e) {
        if(!e) e = window.event;
           //e.cancelBubble is supported by IE - this will kill the bubbling process.
           e.cancelBubble = true;
           e.returnValue = 'You sure you want to leave?'; //This is displayed on the dialog
           
           //e.stopPropagation works in Firefox.
           if (e.stopPropagation) {
           e.stopPropagation();
           e.preventDefault();
           }
}

window.onbeforeunload=goodbye;

$(document).ready(function() { window.setInterval(function(e) { window.Shiny.shinyapp.$socket.send('{\"method\":\"none\"}'); }, 140000); });
")

appCSS <- "
#loading-content {
position: absolute;
background: #FFFFFF;
opacity: 0.9;
z-index: 100;
left: 0;
right: 0;
height: 100%;
text-align: center;
color: #FFFFFF;
}
"

ui <-  fluidPage(
  theme = "bootstrap_spacelab.css",
  tags$head(tags$link(rel = "stylesheet", type = "text/css", href = "custom.css")),
  tags$head(tags$script(detectclosejs)),tags$head(tags$script(HTML('
                             Shiny.addCustomMessageHandler("jsCode",
                                                                   function(message) {
                                                                   console.log(message)
                                                                   eval(message.code);
                                                                   }
  );
                                                                   '))),
  tags$head(tags$script('
                                var dimension = [0, 0];
                        $(document).on("shiny:connected", function(e) {
                        dimension[0] = window.innerWidth;
                        dimension[1] = window.innerHeight;
                        Shiny.onInputChange("dimension", dimension);
                        });
                        $(window).resize(function(e) {
                        dimension[0] = window.innerWidth;
                        dimension[1] = window.innerHeight;
                        Shiny.onInputChange("dimension", dimension);
                        });
                        ')),
  useShinyjs(),
  extendShinyjs(text ='
                      shinyjs.show_modal = function(who){
                        $("#"+who).css("display", "block");
                      }
                      shinyjs.hide_modal = function(who){
                        $("#"+who).css("display", "none");
                      }
                      ', functions = c('show_modal', 'hide_modal')),
  inlineCSS(appCSS),
  # Loading message
  div(
    id = "loading-content",
    h2("Loading Lagun...")
  ),
  div(id="hold_modal",style=ifelse(Sys.getenv("TEST")==1,"display:block; ","display:none; "),
      actionButton(inputId = "close_modal","X",style="display:block; position:fixed; top:0px; right:0px; z-index:10005; width:50px; height:50px"),
      uiOutput(outputId = "layer_modal",inline = T,container = div,style="display:none; position:fixed; top:0px; left:0px; z-index:10002; width:100%; height:100%")
  ),
  hidden(
    div(
      id = "app-content",
      navigation.ui(id = "nav")
    )
  )
)

server <- function(input, output, session) {
  
  window.dimension <- reactiveValues(width=NULL, height = NULL)
  observeEvent(input$dimension,{
    window.dimension$width <- input$dimension[1]
    window.dimension$height <- input$dimension[2]
  })
  
  callModule(navigation.server, "nav", window.dimension)
  
  # Hide the loading message when the rest of the server function has executed
  shinyjs::hide(id = "loading-content", anim = TRUE, animType = "fade")   
  shinyjs::show("app-content")
  
  output$layer_modal <- renderUI("X") # need to be declared here. Even if stays empty out of test...
  if (Sys.getenv("TEST")==1) {
    show_modal <<- function(who,content){
      output$layer_modal <- renderUI(content)
      warning('show_modal("',who,'")')
      opened_modals <<-  c(opened_modals,who)
      print(opened_modals)
      
      js$show_modal("layer_modal")
    }
    hide_modal <<- function(who) {
      warning('hide_modal("',who,'")')
      if (who %in% opened_modals)
        opened_modals <<- opened_modals[-which(opened_modals == who)]
      print(opened_modals)
      
      js$hide_modal(who)  
    }
    observeEvent(input$close_modal,{
      hide_modal(opened_modals[length(opened_modals)])
    })
  }
  
}

# app
shinyApp(ui, server)
