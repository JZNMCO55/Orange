#include "ImGraphs.h"
#include "imgui/imgui.h"
#include "ImGui/ImGuizmo/ImGuizmo.h"
#include <ImGui/ImPlot/implot.h>
#include <stdlib.h>
#include <cmath>
#define CHECKBOX_FLAG(flags, flag) ImGui::CheckboxFlags(#flag, (unsigned int*)&flags, flag)

namespace 
{
    double RandomGauss() 
    {
        static double V1, V2, S;
        static int phase = 0;
        double X;
        if (phase == 0) 
        {
            do {
                double U1 = (double)rand() / RAND_MAX;
                double U2 = (double)rand() / RAND_MAX;
                V1 = 2 * U1 - 1;
                V2 = 2 * U2 - 1;
                S = V1 * V1 + V2 * V2;
            } while (S >= 1 || S == 0);

            X = V1 * sqrt(-2 * log(S) / S);
        }
        else
            X = V2 * sqrt(-2 * log(S) / S);
        phase = 1 - phase;
        return X;
    }
    template <typename T>
    inline T RandomRange(T min, T max) 
    {
        T scale = rand() / (T)RAND_MAX;
        return min + scale * (max - min);
    }

    template <int N>
    struct NormalDistribution {
        NormalDistribution(double mean, double sd) {
            for (int i = 0; i < N; ++i)
                Data[i] = RandomGauss() * sd + mean;
        }
        double Data[N];
    };
    void Demo_FilledLinePlots()
    {
        ImGui::Begin("Filled Line Plots");
        static double xs1[101], ys1[101], ys2[101], ys3[101];
        srand(0);
        for (int i = 0; i < 101; ++i) {
            xs1[i] = (float)i;
            ys1[i] = RandomRange(400.0,450.0);
            ys2[i] = RandomRange(275.0,350.0);
            ys3[i] = RandomRange(150.0,225.0);
        }
        static bool show_lines = true;
        static bool show_fills = true;
        static float fill_ref = 0;
        static int shade_mode = 0;
        static ImPlotShadedFlags flags = 0;
        ImGui::Checkbox("Lines",&show_lines); ImGui::SameLine();
        ImGui::Checkbox("Fills",&show_fills);
        if (show_fills) {
            ImGui::SameLine();
            if (ImGui::RadioButton("To -INF",shade_mode == 0))
                shade_mode = 0;
            ImGui::SameLine();
            if (ImGui::RadioButton("To +INF",shade_mode == 1))
                shade_mode = 1;
            ImGui::SameLine();
            if (ImGui::RadioButton("To Ref",shade_mode == 2))
                shade_mode = 2;
            if (shade_mode == 2) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100);
                ImGui::DragFloat("##Ref",&fill_ref, 1, -100, 500);
            }
        }
    
        if (ImPlot::BeginPlot("Stock Prices")) {
            ImPlot::SetupAxes("Days","Price");
            ImPlot::SetupAxesLimits(0,100,0,500);
            if (show_fills) {
                ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.25f);
                ImPlot::PlotShaded("Stock 1", xs1, ys1, 101, shade_mode == 0 ? -INFINITY : shade_mode == 1 ? INFINITY : fill_ref, flags);
                ImPlot::PlotShaded("Stock 2", xs1, ys2, 101, shade_mode == 0 ? -INFINITY : shade_mode == 1 ? INFINITY : fill_ref, flags);
                ImPlot::PlotShaded("Stock 3", xs1, ys3, 101, shade_mode == 0 ? -INFINITY : shade_mode == 1 ? INFINITY : fill_ref, flags);
                ImPlot::PopStyleVar();
            }
            if (show_lines) {
                ImPlot::PlotLine("Stock 1", xs1, ys1, 101);
                ImPlot::PlotLine("Stock 2", xs1, ys2, 101);
                ImPlot::PlotLine("Stock 3", xs1, ys3, 101);
            }
            ImPlot::EndPlot();
        }
        ImGui::End();
    }

    void Demo_ShadedPlots() 
    {
        ImGui::Begin("Shaded Plots");
        static float xs[1001], ys[1001], ys1[1001], ys2[1001], ys3[1001], ys4[1001];
        srand(0);
        for (int i = 0; i < 1001; ++i) {
            xs[i]  = i * 0.001f;
            ys[i]  = 0.25f + 0.25f * sinf(25 * xs[i]) * sinf(5 * xs[i]) + RandomRange(-0.01f, 0.01f);
            ys1[i] = ys[i] + RandomRange(0.1f, 0.12f);
            ys2[i] = ys[i] - RandomRange(0.1f, 0.12f);
            ys3[i] = 0.75f + 0.2f * sinf(25 * xs[i]);
            ys4[i] = 0.75f + 0.1f * cosf(25 * xs[i]);
        }
        static float alpha = 0.25f;
        ImGui::DragFloat("Alpha",&alpha,0.01f,0,1);
    
        if (ImPlot::BeginPlot("Shaded Plots")) {
            ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, alpha);
            ImPlot::PlotShaded("Uncertain Data",xs,ys1,ys2,1001);
            ImPlot::PlotLine("Uncertain Data", xs, ys, 1001);
            ImPlot::PlotShaded("Overlapping",xs,ys3,ys4,1001);
            ImPlot::PlotLine("Overlapping",xs,ys3,1001);
            ImPlot::PlotLine("Overlapping",xs,ys4,1001);
            ImPlot::PopStyleVar();
            ImPlot::EndPlot();
        }
        ImGui::End();
    }

    void Demo_ScatterPlots() 
    {
        ImGui::Begin("Scatter Plots");
        srand(0);
        static float xs1[100], ys1[100];
        for (int i = 0; i < 100; ++i) {
            xs1[i] = i * 0.01f;
            ys1[i] = xs1[i] + 0.1f * ((float)rand() / (float)RAND_MAX);
        }
        static float xs2[50], ys2[50];
        for (int i = 0; i < 50; i++) {
            xs2[i] = 0.25f + 0.2f * ((float)rand() / (float)RAND_MAX);
            ys2[i] = 0.75f + 0.2f * ((float)rand() / (float)RAND_MAX);
        }
    
        if (ImPlot::BeginPlot("Scatter Plot")) {
            ImPlot::PlotScatter("Data 1", xs1, ys1, 100);
            ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.25f);
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Square, 6, ImPlot::GetColormapColor(1), IMPLOT_AUTO, ImPlot::GetColormapColor(1));
            ImPlot::PlotScatter("Data 2", xs2, ys2, 50);
            ImPlot::PopStyleVar();
            ImPlot::EndPlot();
        }
        ImGui::End();
    }

    void Demo_BarStacks() 
    {
        ImGui::Begin("Bar Stacks");
        static ImPlotColormap Liars = -1;
        if (Liars == -1) {
            static const ImU32 Liars_Data[6] = { 4282515870, 4282609140, 4287357182, 4294630301, 4294945280, 4294921472 };
            Liars = ImPlot::AddColormap("Liars", Liars_Data, 6);
        }
    
        static bool diverging = true;
        ImGui::Checkbox("Diverging",&diverging);
    
        static const char* politicians[] = {"Trump","Bachman","Cruz","Gingrich","Palin","Santorum","Walker","Perry","Ryan","McCain","Rubio","Romney","Rand Paul","Christie","Biden","Kasich","Sanders","J Bush","H Clinton","Obama"};
        static int data_reg[] = {18,26,7,14,10,8,6,11,4,4,3,8,6,8,6,5,0,3,1,2,                // Pants on Fire
                                 43,36,30,21,30,27,25,17,11,22,15,16,16,17,12,12,14,6,13,12,  // False
                                 16,13,28,22,15,21,15,18,30,17,24,18,13,10,14,15,17,22,14,12, // Mostly False
                                 17,10,13,25,12,22,19,26,23,17,22,27,20,26,29,17,18,22,21,27, // Half True
                                 5,7,16,10,10,12,23,13,17,20,22,16,23,19,20,26,36,29,27,26,   // Mostly True
                                 1,8,6,8,23,10,12,15,15,20,14,15,22,20,19,25,15,18,24,21};    // True
        static const char* labels_reg[] = {"Pants on Fire","False","Mostly False","Half True","Mostly True","True"};
    
    
        static int data_div[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,                                         // Pants on Fire (dummy, to order legend logically)
                                 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,                                         // False         (dummy, to order legend logically)
                                 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,                                         // Mostly False  (dummy, to order legend logically)
                                 -16,-13,-28,-22,-15,-21,-15,-18,-30,-17,-24,-18,-13,-10,-14,-15,-17,-22,-14,-12, // Mostly False
                                 -43,-36,-30,-21,-30,-27,-25,-17,-11,-22,-15,-16,-16,-17,-12,-12,-14,-6,-13,-12,  // False
                                 -18,-26,-7,-14,-10,-8,-6,-11,-4,-4,-3,-8,-6,-8,-6,-5,0,-3,-1,-2,                 // Pants on Fire
                                 17,10,13,25,12,22,19,26,23,17,22,27,20,26,29,17,18,22,21,27,                     // Half True
                                 5,7,16,10,10,12,23,13,17,20,22,16,23,19,20,26,36,29,27,26,                       // Mostly True
                                 1,8,6,8,23,10,12,15,15,20,14,15,22,20,19,25,15,18,24,21};                        // True
        static const char* labels_div[] = {"Pants on Fire","False","Mostly False","Mostly False","False","Pants on Fire","Half True","Mostly True","True"};
    
        ImPlot::PushColormap(Liars);
        if (ImPlot::BeginPlot("PolitiFact: Who Lies More?",ImVec2(-1,400),ImPlotFlags_NoMouseText)) {
            ImPlot::SetupLegend(ImPlotLocation_South, ImPlotLegendFlags_Outside|ImPlotLegendFlags_Horizontal);
            ImPlot::SetupAxes(nullptr,nullptr,ImPlotAxisFlags_AutoFit|ImPlotAxisFlags_NoDecorations,ImPlotAxisFlags_AutoFit|ImPlotAxisFlags_Invert);
            ImPlot::SetupAxisTicks(ImAxis_Y1,0,19,20,politicians,false);
            if (diverging)
                ImPlot::PlotBarGroups(labels_div,data_div,9,20,0.75,0,ImPlotBarGroupsFlags_Stacked|ImPlotBarGroupsFlags_Horizontal);
            else
                ImPlot::PlotBarGroups(labels_reg,data_reg,6,20,0.75,0,ImPlotBarGroupsFlags_Stacked|ImPlotBarGroupsFlags_Horizontal);
            ImPlot::EndPlot();
        }
        ImPlot::PopColormap();
        ImGui::End();
    }
    
    void Demo_StemPlots() 
    {
        ImGui::Begin("Stem Plots");
        static double xs[51], ys1[51], ys2[51];
        for (int i = 0; i < 51; ++i) {
            xs[i] = i * 0.02;
            ys1[i] = 1.0 + 0.5 * sin(25*xs[i])*cos(2*xs[i]);
            ys2[i] = 0.5 + 0.25  * sin(10*xs[i]) * sin(xs[i]);
        }
        if (ImPlot::BeginPlot("Stem Plots")) {
            ImPlot::SetupAxisLimits(ImAxis_X1,0,1.0);
            ImPlot::SetupAxisLimits(ImAxis_Y1,0,1.6);
            ImPlot::PlotStems("Stems 1",xs,ys1,51);
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle);
            ImPlot::PlotStems("Stems 2", xs, ys2,51);
            ImPlot::EndPlot();
        }
        ImGui::End();
    }

    void Demo_PieCharts() 
    {
        ImGui::Begin("Pie Charts");
        static const char* labels1[]    = {"Frogs","Hogs","Dogs","Logs"};
        static float data1[]            = {0.15f,  0.30f,  0.2f, 0.05f};
        static ImPlotPieChartFlags flags = 0;
        ImGui::SetNextItemWidth(250);
        ImGui::DragFloat4("Values", data1, 0.01f, 0, 1);
        CHECKBOX_FLAG(flags, ImPlotPieChartFlags_Normalize);
        CHECKBOX_FLAG(flags, ImPlotPieChartFlags_IgnoreHidden);
        CHECKBOX_FLAG(flags, ImPlotPieChartFlags_Exploding);
    
        if (ImPlot::BeginPlot("##Pie1", ImVec2(250,250), ImPlotFlags_Equal | ImPlotFlags_NoMouseText)) {
            ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoDecorations);
            ImPlot::SetupAxesLimits(0, 1, 0, 1);
            ImPlot::PlotPieChart(labels1, data1, 4, 0.5, 0.5, 0.4, "%.2f", 90, flags);
            ImPlot::EndPlot();
        }
    
        ImGui::SameLine();
    
        static const char* labels2[]   = {"A","B","C","D","E"};
        static int data2[]             = {1,1,2,3,5};
    
        ImPlot::PushColormap(ImPlotColormap_Pastel);
        if (ImPlot::BeginPlot("##Pie2", ImVec2(250,250), ImPlotFlags_Equal | ImPlotFlags_NoMouseText)) {
            ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoDecorations);
            ImPlot::SetupAxesLimits(0, 1, 0, 1);
            ImPlot::PlotPieChart(labels2, data2, 5, 0.5, 0.5, 0.4, "%.0f", 180, flags);
            ImPlot::EndPlot();
        }
        ImPlot::PopColormap();
        ImGui::End();
    }

    void Demo_Histogram() 
    {
        ImGui::Begin("Histogram");
        static ImPlotHistogramFlags hist_flags = ImPlotHistogramFlags_Density;
        static int  bins       = 50;
        static double mu       = 5;
        static double sigma    = 2;
        ImGui::SetNextItemWidth(200);
        if (ImGui::RadioButton("Sqrt",bins==ImPlotBin_Sqrt))       { bins = ImPlotBin_Sqrt;    } ImGui::SameLine();
        if (ImGui::RadioButton("Sturges",bins==ImPlotBin_Sturges)) { bins = ImPlotBin_Sturges; } ImGui::SameLine();
        if (ImGui::RadioButton("Rice",bins==ImPlotBin_Rice))       { bins = ImPlotBin_Rice;    } ImGui::SameLine();
        if (ImGui::RadioButton("Scott",bins==ImPlotBin_Scott))     { bins = ImPlotBin_Scott;   } ImGui::SameLine();
        if (ImGui::RadioButton("N Bins",bins>=0))                  { bins = 50;                }
        if (bins>=0) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200);
            ImGui::SliderInt("##Bins", &bins, 1, 100);
        }
        ImGui::CheckboxFlags("Horizontal", (unsigned int*)&hist_flags, ImPlotHistogramFlags_Horizontal);
        ImGui::SameLine();
        ImGui::CheckboxFlags("Density", (unsigned int*)&hist_flags, ImPlotHistogramFlags_Density);
        ImGui::SameLine();
        ImGui::CheckboxFlags("Cumulative", (unsigned int*)&hist_flags, ImPlotHistogramFlags_Cumulative);
    
        static bool range = false;
        ImGui::Checkbox("Range", &range);
        static float rmin = -3;
        static float rmax = 13;
        if (range) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200);
            ImGui::DragFloat2("##Range",&rmin,0.1f,-3,13);
            ImGui::SameLine();
            ImGui::CheckboxFlags("Exclude Outliers", (unsigned int*)&hist_flags, ImPlotHistogramFlags_NoOutliers);
        }
        static NormalDistribution<10000> dist(mu, sigma);
        static double x[100];
        static double y[100];
        if (hist_flags & ImPlotHistogramFlags_Density) {
            for (int i = 0; i < 100; ++i) {
                x[i] = -3 + 16 * (double)i/99.0;
                y[i] = exp( - (x[i]-mu)*(x[i]-mu) / (2*sigma*sigma)) / (sigma * sqrt(2*3.141592653589793238));
            }
            if (hist_flags & ImPlotHistogramFlags_Cumulative) {
                for (int i = 1; i < 100; ++i)
                    y[i] += y[i-1];
                for (int i = 0; i < 100; ++i)
                    y[i] /= y[99];
            }
        }
    
        if (ImPlot::BeginPlot("##Histograms")) {
            ImPlot::SetupAxes(nullptr,nullptr,ImPlotAxisFlags_AutoFit,ImPlotAxisFlags_AutoFit);
            ImPlot::SetNextFillStyle(IMPLOT_AUTO_COL,0.5f);
            ImPlot::PlotHistogram("Empirical", dist.Data, 10000, bins, 1.0, range ? ImPlotRange(rmin,rmax) : ImPlotRange(), hist_flags);
            if ((hist_flags & ImPlotHistogramFlags_Density) && !(hist_flags & ImPlotHistogramFlags_NoOutliers)) {
                if (hist_flags & ImPlotHistogramFlags_Horizontal)
                    ImPlot::PlotLine("Theoretical",y,x,100);
                else
                    ImPlot::PlotLine("Theoretical",x,y,100);
            }
            ImPlot::EndPlot();
        }
        ImGui::End();
    }
    
}

namespace Orange
{
    void ImGraphs::DrawGraphs()
    {
        ::Demo_FilledLinePlots();
        ::Demo_ShadedPlots();
        ::Demo_ScatterPlots();
        ::Demo_BarStacks();
        ::Demo_StemPlots();
        ::Demo_PieCharts();
        ::Demo_Histogram();
    }
}
