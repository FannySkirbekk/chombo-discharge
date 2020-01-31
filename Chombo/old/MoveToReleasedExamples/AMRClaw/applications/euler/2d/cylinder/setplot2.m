
%  setplot2.m
%  called in plotclaw1 before plotting to set various parameters
OutputFlag = 'ascii';
plot_interval = 1;
plot_prefix = 'plotFINE';

PlotType = 1;                % type of plot to produce:
			     % 1 = pseudo-color (pcolor)
			     % 2 = contour
			     % 3 = Schlieren
		 	     % 4 = scatter plot of q vs. r

mq = 1;                      % which component of q to plot
UserVariable = 0;            % set to 1 to specify a user-defined variable
UserVariableFile = '';      % name of m-file mapping data to q
MappedGrid = 1;              % set to 1 if mapc2p.m exists for nonuniform grid
MaxFrames = 1000;            % max number of frames to loop over
MaxLevels = 6;
PlotData =  [1 1 1 1 1 1];   % Data on refinement level k is plotted only if
			     % kth component is nonzero
PlotGrid =  [0 0 0 0 0 0];   % Plot grid lines on each level?

PlotGridEdges =  [1 1 1 1 1 1];  % Plot edges of patches of each grid at
                                 % this level?

PlotCubeEdges = [0 0 0];
%---------------------------------

% for contour plots (PlotType==2):
ContourValues = [linspace(1,6,50)];
